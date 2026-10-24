// admin.cpp - local control channel: rooms / read / users / kick / close.
#include "admin.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>

namespace lt {

namespace {

std::vector<std::string> split_words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string w;
    while (iss >> w) out.push_back(w);
    return out;
}

const char* kHelp =
    "命令：\n"
    "  rooms                 列出所有房间（房号 / 人数 / 消息数 / 成员）\n"
    "  users <房号>          列出该房间的在线用户\n"
    "  read  <房号> [条数]   查看聊天记录（默认最近 50 条，不会通知用户）\n"
    "  kick  <房号> <昵称>   把某个用户移出房间\n"
    "  close <房号>          立即关闭房间并删除记录\n"
    "  help                  显示本帮助\n";

}  // namespace

Admin::Admin(Hub& hub, std::string path) : hub_(hub), path_(std::move(path)) {}

int Admin::open() {
    if (path_.empty()) return -1;
    if (path_.size() >= sizeof(sockaddr_un::sun_path)) {
        std::fprintf(stderr, "admin: socket path too long: %s\n", path_.c_str());
        return -1;
    }

    lfd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (lfd_ < 0) {
        std::fprintf(stderr, "admin: socket: %s\n", std::strerror(errno));
        return -1;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);

    ::unlink(path_.c_str());  // stale socket from a previous run
    ::umask(0077);
    if (::bind(lfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::fprintf(stderr, "admin: bind %s: %s\n", path_.c_str(), std::strerror(errno));
        ::close(lfd_);
        lfd_ = -1;
        return -1;
    }
    ::chmod(path_.c_str(), 0600);
    if (::listen(lfd_, 8) != 0) {
        std::fprintf(stderr, "admin: listen: %s\n", std::strerror(errno));
        ::close(lfd_);
        lfd_ = -1;
        return -1;
    }
    std::fprintf(stderr, "admin socket ready: %s (local only, mode 0600)\n", path_.c_str());
    return lfd_;
}

std::vector<int> Admin::accept_all() {
    std::vector<int> fds;
    for (;;) {
        int fd = ::accept4(lfd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            break;
        }
        bufs_[fd].clear();
        fds.push_back(fd);
    }
    return fds;
}

void Admin::forget(int fd) { bufs_.erase(fd); }

bool Admin::on_readable(int fd) {
    auto it = bufs_.find(fd);
    if (it == bufs_.end()) return false;

    char buf[512];
    for (;;) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            it->second.append(buf, static_cast<size_t>(n));
            if (it->second.size() > 4096) break;  // 防止有人塞垃圾
            continue;
        }
        if (n == 0) break;  // peer closed: process whatever we have
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // wait for newline
        break;
    }

    std::string line = it->second;
    size_t nl = line.find('\n');
    if (nl != std::string::npos) line = line.substr(0, nl);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();

    std::string out = line.empty() ? std::string(kHelp) : run(line);
    reply(fd, out);
    return false;  // one command per connection
}

void Admin::reply(int fd, const std::string& text) {
    size_t off = 0;
    while (off < text.size()) {
        ssize_t n = ::send(fd, text.data() + off, text.size() - off, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        off += static_cast<size_t>(n);
    }
}

std::string Admin::run(const std::string& line) {
    std::vector<std::string> w = split_words(line);
    if (w.empty()) return kHelp;

    const std::string& cmd = w[0];
    if (cmd == "help") return kHelp;

    if (cmd == "rooms") {
        auto rooms = hub_.rooms_info();
        if (rooms.empty()) return "（当前没有房间）\n";
        std::string out = "房号      人数  消息  成员\n";
        char buf[512];
        for (const auto& r : rooms) {
            std::snprintf(buf, sizeof(buf), "%-8s  %3zu   %4zu  %s\n", r.id.c_str(), r.users,
                          r.msgs, r.names.c_str());
            out += buf;
        }
        out += "共 " + std::to_string(rooms.size()) + " 个房间\n";
        return out;
    }

    if (cmd == "users") {
        if (w.size() < 2) return "用法：users <房号>\n";
        auto names = hub_.users_of(w[1]);
        auto rooms = hub_.rooms_info();
        bool found = false;
        for (const auto& r : rooms) if (r.id == w[1]) found = true;
        if (!found) return "房间 " + w[1] + " 不存在\n";
        std::string out = "房间 " + w[1] + "（" + std::to_string(names.size()) + " 人）\n";
        for (const auto& n : names) out += "  " + n + "\n";
        if (names.empty()) out += "  （没有人）\n";
        return out;
    }

    if (cmd == "read") {
        if (w.size() < 2) return "用法：read <房号> [条数]\n";
        size_t limit = 50;
        if (w.size() >= 3) {
            long v = std::strtol(w[2].c_str(), nullptr, 10);
            if (v > 0) limit = static_cast<size_t>(v > 500 ? 500 : v);
        }
        return hub_.history_text(w[1], limit);
    }

    if (cmd == "kick") {
        if (w.size() < 3) return "用法：kick <房号> <昵称>\n";
        int fd = hub_.kick_user(w[1], w[2]);
        if (fd < 0) return "没找到：房间 " + w[1] + " 里的 " + w[2] + "\n";
        pending_.push_back(fd);
        return "已移出 " + w[2] + "（房间 " + w[1] + "）\n";
    }

    if (cmd == "close") {
        if (w.size() < 2) return "用法：close <房号>\n";
        auto fds = hub_.close_room(w[1]);
        for (int fd : fds) pending_.push_back(fd);
        if (fds.empty()) return "房间 " + w[1] + " 不存在或已空\n";
        return "已关闭房间 " + w[1] + "，断开 " + std::to_string(fds.size()) + " 个连接并删除记录\n";
    }

    return "未知命令：" + cmd + "\n" + kHelp;
}

}  // namespace lt
