#include "server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "../util/json.h"
#include "../util/text.h"
#include "../util/time.h"

namespace lt {

namespace {
constexpr int kMaxConns = 256;
constexpr size_t kMaxInBuf = 4 * 1024 * 1024;
constexpr size_t kMaxOutBuf = 1024 * 1024;
constexpr size_t kMaxHead = 16 * 1024;
constexpr size_t kMaxStatic = 4 * 1024 * 1024;
constexpr int kPingAfterMs = 60000;
constexpr int kDropAfterMs = 180000;
constexpr int kIdleHttpMs = 15000;
}  // namespace

Server::Server(int port, std::string bind_addr, std::string webdir, std::string datadir,
               std::string key, uint64_t room_ttl_ms, int max_conns, size_t max_rooms,
               size_t max_users)
    : port_(port), bind_addr_(std::move(bind_addr)), webdir_(std::move(webdir)),
      key_(std::move(key)),
      hub_(std::move(datadir), 100, room_ttl_ms, max_rooms, max_users) {
    room_ttl_ms_ = room_ttl_ms;
    max_conns_ = max_conns > 0 ? max_conns : 128;
}

Server::~Server() {
    for (auto& kv : conns_) ::close(kv.first);
    if (lfd_ >= 0) ::close(lfd_);
    if (ep_ >= 0) ::close(ep_);
}

bool Server::init() {
    hub_.set_sender([this](int fd, const std::string& json) {
        auto it = conns_.find(fd);
        if (it == conns_.end()) return;
        send_raw(it->second, ws_frame(WS_TEXT, json));
    });

    lfd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (lfd_ < 0) {
        std::fprintf(stderr, "socket: %s\n", std::strerror(errno));
        return false;
    }
    int one = 1;
    ::setsockopt(lfd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (bind_addr_.empty() || bind_addr_ == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, bind_addr_.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr, "bad bind address: %s\n", bind_addr_.c_str());
        return false;
    }
    if (::bind(lfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "bind %s:%d: %s\n", bind_addr_.c_str(), port_, std::strerror(errno));
        return false;
    }
    if (::listen(lfd_, 128) != 0) {
        std::fprintf(stderr, "listen: %s\n", std::strerror(errno));
        return false;
    }

    ep_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (ep_ < 0) {
        std::fprintf(stderr, "epoll_create1: %s\n", std::strerror(errno));
        return false;
    }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = lfd_;
    ::epoll_ctl(ep_, EPOLL_CTL_ADD, lfd_, &ev);

    started_ms_ = mono_ms();
    std::fprintf(stderr, "liaotian listening on %s:%d web=%s key=%s rooms_ttl=%llus\n",
                 bind_addr_.c_str(), port_, webdir_.c_str(), key_.empty() ? "(open)" : "(required)",
                 static_cast<unsigned long long>(room_ttl_ms_ / 1000));
    return true;
}

void Server::run() {
    running_ = true;
    epoll_event events[64];
    while (running_) {
        int n = ::epoll_wait(ep_, events, 64, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "epoll_wait: %s\n", std::strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == lfd_) {
                accept_loop();
            } else {
                on_event(fd, events[i].events);
            }
        }
        tick(mono_ms());
    }
    hub_.shutdown();
}

void Server::accept_loop() {
    for (;;) {
        sockaddr_in peer{};
        socklen_t len = sizeof(peer);
        int fd = ::accept4(lfd_, reinterpret_cast<sockaddr*>(&peer), &len,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            if (errno == EINTR) continue;
            return;
        }
        if (static_cast<int>(conns_.size()) >= max_conns_) {
            ::close(fd);
            continue;
        }
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        Conn c;
        c.fd = fd;
        c.last_recv = mono_ms();
        conns_.emplace(fd, std::move(c));

        epoll_event cev{};
        cev.events = EPOLLIN | EPOLLRDHUP;
        cev.data.fd = fd;
        if (::epoll_ctl(ep_, EPOLL_CTL_ADD, fd, &cev) != 0) {
            conns_.erase(fd);
            ::close(fd);
            continue;
        }
    }
}

void Server::update_events(Conn& c) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLRDHUP | (c.out.empty() ? 0u : EPOLLOUT);
    ev.data.fd = c.fd;
    ::epoll_ctl(ep_, EPOLL_CTL_MOD, c.fd, &ev);
}

void Server::on_event(int fd, uint32_t ev) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Conn& c = it->second;

    if (ev & (EPOLLERR | EPOLLHUP)) {
        c.closing = true;
        c.out.clear();
    }
    if (!c.closing && (ev & EPOLLIN)) read_conn(c);
    if (conns_.count(fd) == 0) return;

    Conn& c2 = conns_[fd];
    if (!c2.closing && (ev & EPOLLOUT)) flush(c2);
    if (conns_.count(fd) == 0) return;

    Conn& c3 = conns_[fd];
    if (c3.closing) {
        if (!c3.out.empty()) flush(c3);
        if (conns_.count(fd) && c3.out.empty()) close_conn(fd);
    }
}

void Server::read_conn(Conn& c) {
    char buf[65536];
    for (;;) {
        ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
        if (n > 0) {
            if (c.in.size() + static_cast<size_t>(n) > kMaxInBuf) {
                c.closing = true;
                return;
            }
            c.in.append(buf, static_cast<size_t>(n));
            c.last_recv = mono_ms();
            continue;
        }
        if (n == 0) {
            c.closing = true;
            return;
        }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        c.closing = true;
        return;
    }
    process(c);
}

void Server::process(Conn& c) {
    for (;;) {
        if (c.closing) return;

        if (!c.ws) {
            size_t pos = c.in.find("\r\n\r\n");
            if (pos == std::string::npos) {
                if (c.in.size() > kMaxHead) {
                    send_response(c, 431, "text/plain; charset=utf-8", "header too large\n", false);
                    c.closing = true;
                }
                return;
            }
            std::string head = c.in.substr(0, pos + 4);
            c.in.erase(0, pos + 4);

            HttpRequest req;
            if (!http_parse(head, req)) {
                send_response(c, 400, "text/plain", "bad request\n", false);
                c.closing = true;
                return;
            }

            std::string up;
            bool is_upgrade = http_header(req, "upgrade", up) && lower(up) == "websocket";
            if (is_upgrade) {
                if (!handle_ws_handshake(c, req, head)) return;
                continue;  // leftover bytes may already contain frames
            }

            bool keep = true;
            std::string conn_hdr;
            if (http_header(req, "connection", conn_hdr) && lower(conn_hdr) == "close") keep = false;
            if (!handle_http(c, req, keep)) {
                c.closing = true;
                return;
            }
            if (!keep) {
                c.closing = true;
                return;
            }
            continue;
        }

        if (!c.in.empty()) {
            c.parser.feed(c.in.data(), c.in.size());
            c.in.clear();
        }
        for (;;) {
            WsFrame f;
            std::string err;
            int r = c.parser.next(f, err);
            if (r == 0) return;
            if (r < 0) {
                std::fprintf(stderr, "ws protocol error fd=%d: %s\n", c.fd, err.c_str());
                c.closing = true;
                return;
            }

            if (f.opcode == WS_CLOSE) {
                send_raw(c, ws_close(1000, "bye"));
                c.closing = true;
                return;
            }
            if (f.opcode == WS_PING) {
                send_raw(c, ws_frame(WS_PONG, f.payload));
                continue;
            }
            if (f.opcode == WS_PONG) continue;

            if (f.opcode == WS_TEXT || f.opcode == WS_BIN) {
                c.frag = f.payload;
                c.frag_op = f.opcode;
                if (!f.fin) continue;
            } else if (f.opcode == WS_CONT) {
                if (c.frag_op == 0) {
                    c.closing = true;
                    return;
                }
                c.frag += f.payload;
                if (!f.fin) continue;
            } else {
                continue;
            }

            if (c.frag_op == WS_TEXT) hub_.on_text(c.fd, c.frag);
            c.frag.clear();
            c.frag_op = 0;
            if (c.closing) return;
        }
    }
}

bool Server::handle_http(Conn& c, const HttpRequest& req, bool keep_alive) {
    if (req.method != "GET" && req.method != "HEAD") {
        send_response(c, 405, "text/plain; charset=utf-8", "method not allowed\n", false);
        return false;
    }

    if (req.path == "/api/health") {
        std::string body = "{\"ok\":true,\"online\":" + std::to_string(hub_.online()) +
                           ",\"rooms\":" + std::to_string(hub_.active_rooms()) +
                           ",\"rooms_total\":" + std::to_string(hub_.room_count()) +
                           ",\"uptime\":" + std::to_string((mono_ms() - started_ms_) / 1000) +
                           ",\"version\":\"2.0\"}";
        send_response(c, 200, "application/json; charset=utf-8", body, keep_alive);
        return keep_alive;
    }


    if (req.path == "/api/history") {
        std::string room = query_param(req.query, "room");
        size_t limit = 50;
        std::string ls = query_param(req.query, "limit");
        if (!ls.empty()) {
            long v = std::strtol(ls.c_str(), nullptr, 10);
            if (v > 0) limit = static_cast<size_t>(v > 200 ? 200 : v);
        }
        std::string body = "{\"messages\":" + hub_.history_json(room, limit) + "}";
        send_response(c, 200, "application/json; charset=utf-8", body, keep_alive);
        return keep_alive;
    }

    if (req.path == "/ws") {
        send_response(c, 400, "text/plain", "websocket upgrade required\n", false);
        return false;
    }

    serve_static(c, req.path, keep_alive);
    return keep_alive;
}

bool Server::handle_ws_handshake(Conn& c, const HttpRequest& req, const std::string& head) {
    (void)head;
    if (!key_.empty()) {
        std::string given = query_param(req.query, "k");
        if (given != key_) {
            send_response(c, 403, "text/plain; charset=utf-8", "invalid room key\n", false);
            c.closing = true;
            return false;
        }
    }

    std::string key;
    if (!http_header(req, "sec-websocket-key", key) || key.empty()) {
        send_response(c, 400, "text/plain", "missing key\n", false);
        c.closing = true;
        return false;
    }

    c.ws = true;
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + ws_accept(trim(key)) + "\r\n\r\n";
    c.out += resp;
    flush(c);
    return true;
}

void Server::serve_static(Conn& c, const std::string& path_in, bool keep_alive) {
    std::string path = path_in;
    if (path == "/" || path.empty()) path = "/index.html";
    if (!safe_rel_path(path)) {
        send_response(c, 404, "text/plain; charset=utf-8", "not found\n", keep_alive);
        return;
    }
    std::string body;
    if (!read_file(webdir_ + path, body, kMaxStatic)) {
        send_response(c, 404, "text/plain; charset=utf-8", "not found\n", keep_alive);
        return;
    }
    // 让 Cloudflare / 浏览器缓存静态资源；HTML 保持不缓存，改动即时生效
    bool is_html = (path.size() > 5 && path.compare(path.size() - 5, 5, ".html") == 0);
    std::string cache = is_html ? "no-cache" : "public, max-age=3600";
    std::string out = http_response(200, mime_for(path), body, keep_alive);
    out.insert(out.find("\r\n") + 2, "Cache-Control: " + cache + "\r\n");
    c.out += out;
    if (c.out.size() > kMaxOutBuf) c.closing = true;
    flush(c);
}

void Server::send_response(Conn& c, int code, const std::string& ctype, const std::string& body,
                           bool keep_alive) {
    c.out += http_response(code, ctype, body, keep_alive);
    if (!keep_alive) c.closing = true;
    flush(c);
}

void Server::send_raw(Conn& c, const std::string& data) {
    if (c.closing) return;
    c.out += data;
    if (c.out.size() > kMaxOutBuf) {
        c.closing = true;
        return;
    }
    flush(c);
}

void Server::flush(Conn& c) {
    while (!c.out.empty()) {
        ssize_t n = ::send(c.fd, c.out.data(), c.out.size(), MSG_NOSIGNAL);
        if (n > 0) {
            c.out.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        c.closing = true;
        c.out.clear();
        return;
    }
    if (conns_.count(c.fd)) update_events(c);
}

void Server::close_conn(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    hub_.on_disconnect(fd);
    ::epoll_ctl(ep_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    conns_.erase(it);
}

void Server::tick(uint64_t now) {
    std::vector<int> dead;
    for (auto& kv : conns_) {
        Conn& c = kv.second;
        if (!c.ws) {
            if (now - c.last_recv > static_cast<uint64_t>(kIdleHttpMs)) dead.push_back(c.fd);
            continue;
        }
        uint64_t idle = now - c.last_recv;
        if (idle > static_cast<uint64_t>(kDropAfterMs)) {
            dead.push_back(c.fd);
            continue;
        }
        if (idle > static_cast<uint64_t>(kPingAfterMs) &&
            now - c.last_ping > static_cast<uint64_t>(kPingAfterMs)) {
            c.last_ping = now;
            send_raw(c, ws_frame(WS_PING, ""));
        }
    }
    for (int fd : dead) close_conn(fd);

    static uint64_t last_gc = 0;
    if (now - last_gc > 5000) {
        last_gc = now;
        hub_.tick(now);
    }
}

}  // namespace lt
