// admin.h - a local-only control channel over a unix socket.
//
// 只有本机（且是 socket 文件属主/root）能连，不经过网络、不暴露端口。
// 协议：一行一条命令，服务端回一段文本后关闭连接。
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../chat/hub.h"

namespace lt {

class Admin {
public:
    Admin(Hub& hub, std::string path);

    // Creates the listening unix socket (0600). Returns -1 when disabled.
    int open();

    // Accepts pending connections, returns their fds for the caller to poll.
    std::vector<int> accept_all();

    // Handles one line from `fd`. Returns false when the connection is done.
    bool on_readable(int fd);
    void forget(int fd);

    // 命令执行后需要由调用方关闭的连接（被踢的用户 / 被关房间的成员）
    std::vector<int> take_pending() {
        std::vector<int> out;
        out.swap(pending_);
        return out;
    }

    int listen_fd() const { return lfd_; }
    const std::string& path() const { return path_; }

private:
    std::string run(const std::string& line);
    void reply(int fd, const std::string& text);

    Hub& hub_;
    std::string path_;
    int lfd_ = -1;
    std::unordered_map<int, std::string> bufs_;
    std::vector<int> pending_;
};

}  // namespace lt
