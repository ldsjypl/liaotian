// main.cpp - argument parsing and process start-up.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>

#include "core/server.h"

namespace {

lt::Server* g_server = nullptr;

void on_signal(int) {
    if (g_server) g_server->stop();
}

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s [options]\n"
                 "  --port N         listen port (default 8081)\n"
                 "  --bind ADDR      bind address (default 127.0.0.1)\n"
                 "  --web DIR        static files directory (default ./web)\n"
                 "  --data DIR       data directory (default ./data)\n"
                 "  --key CODE       optional invite code for the websocket\n"
                 "  --room-ttl SEC   reap a room this long after it becomes empty (default 120)\n"
                 "  --max-conns N    max simultaneous connections (default 128)\n"
                 "  --max-rooms N    max live rooms (default 50)\n"
                 "  --max-users N    max users per room (default 30)\n"
                 "  --admin-sock P   unix socket for the local admin CLI (default /run/liaotian/admin.sock)\n"
                 "  -h, --help       this text\n",
                 argv0);
}

}  // namespace

namespace {

// 房间只在有人时存在，所以进程启动时目录里残留的 <6位房号>.jsonl 一定是
// 上次异常退出留下的孤儿文件，直接清掉，避免长期堆积。
void reap_orphan_rooms(const std::string& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    int removed = 0;
    while (dirent* e = ::readdir(d)) {
        std::string name = e->d_name;
        size_t dot = name.rfind(".jsonl");
        if (dot == std::string::npos || dot != name.size() - 6) continue;
        if (dot != 6) continue;
        bool digits = true;
        for (size_t i = 0; i < 6; ++i)
            if (name[i] < '0' || name[i] > '9') { digits = false; break; }
        if (!digits) continue;
        if (::unlink((dir + "/" + name).c_str()) == 0) ++removed;
    }
    ::closedir(d);
    if (removed) std::fprintf(stderr, "cleaned %d orphan room file(s) in %s\n", removed, dir.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8081;
    std::string bind_addr = "127.0.0.1";
    std::string webdir = "./web";
    std::string datadir = "./data";
    std::string key;
    int room_ttl_sec = 120;
    int max_conns = 128;
    int max_rooms = 50;
    int max_users = 30;
    std::string admin_sock = "/run/liaotian/admin.sock";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--port") {
            port = std::atoi(need_value("--port").c_str());
        } else if (a == "--bind") {
            bind_addr = need_value("--bind");
        } else if (a == "--web") {
            webdir = need_value("--web");
        } else if (a == "--data") {
            datadir = need_value("--data");
        } else if (a == "--key") {
            key = need_value("--key");
        } else if (a == "--room-ttl") {
            room_ttl_sec = std::atoi(need_value("--room-ttl").c_str());
        } else if (a == "--admin-sock") {
            admin_sock = need_value("--admin-sock");
        } else if (a == "--max-conns") {
            max_conns = std::atoi(need_value("--max-conns").c_str());
        } else if (a == "--max-rooms") {
            max_rooms = std::atoi(need_value("--max-rooms").c_str());
        } else if (a == "--max-users") {
            max_users = std::atoi(need_value("--max-users").c_str());
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        }
    }
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "bad port\n");
        return 2;
    }
    if (room_ttl_sec < 10) room_ttl_sec = 10;

    if (::mkdir(datadir.c_str(), 0755) != 0 && errno != EEXIST) {
        std::fprintf(stderr, "cannot create %s: %s\n", datadir.c_str(), std::strerror(errno));
        return 1;
    }

    reap_orphan_rooms(datadir);
    std::signal(SIGPIPE, SIG_IGN);

    lt::Server server(port, bind_addr, webdir, datadir, key,
                      static_cast<uint64_t>(room_ttl_sec) * 1000, max_conns,
                      static_cast<size_t>(max_rooms), static_cast<size_t>(max_users),
                      admin_sock);
    g_server = &server;
    if (!server.init()) return 1;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    server.run();
    std::fprintf(stderr, "liaotian stopped\n");
    return 0;
}
