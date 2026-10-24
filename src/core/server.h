// server.h - epoll event loop, HTTP routing, websocket plumbing.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../chat/hub.h"
#include "admin.h"
#include "../net/websocket.h"
#include "http.h"

namespace lt {

class Server {
public:
    Server(int port, std::string bind_addr, std::string webdir, std::string datadir,
           std::string key, uint64_t room_ttl_ms, int max_conns, size_t max_rooms,
           size_t max_users, std::string admin_sock);
    ~Server();

    bool init();
    void run();
    void stop() { running_ = false; }

private:
    struct Conn {
        int fd = -1;
        std::string in;
        std::string out;
        WsParser parser;
        bool ws = false;
        bool closing = false;
        uint64_t last_recv = 0;
        uint64_t last_ping = 0;
        std::string frag;
        int frag_op = 0;
    };

    void accept_loop();
    void on_event(int fd, uint32_t ev);
    void read_conn(Conn& c);
    void process(Conn& c);
    bool handle_http(Conn& c, const HttpRequest& req, bool keep_alive);
    bool handle_ws_handshake(Conn& c, const HttpRequest& req, const std::string& head);
    void serve_static(Conn& c, const std::string& path, bool keep_alive);
    void send_response(Conn& c, int code, const std::string& ctype, const std::string& body,
                       bool keep_alive);
    void send_raw(Conn& c, const std::string& data);
    void flush(Conn& c);
    void close_conn(int fd);
    void update_events(Conn& c);
    void tick(uint64_t now);

    int port_;
    std::string bind_addr_;
    std::string webdir_;
    std::string key_;
    int lfd_ = -1;
    int ep_ = -1;
    bool running_ = false;
    std::unordered_map<int, Conn> conns_;
    Hub hub_;
    Admin admin_;
    int admin_lfd_ = -1;
    std::unordered_set<int> admin_conns_;
    uint64_t started_ms_ = 0;
    uint64_t room_ttl_ms_ = 120000;
    int max_conns_ = 128;
};

}  // namespace lt
