// hub.h - rooms + membership + the JSON protocol. Knows nothing about sockets.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "room.h"

namespace lt {

class Hub {
public:
    // Sends one JSON text frame to a connection.
    using SendFn = std::function<void(int fd, const std::string& json)>;

    Hub(std::string dir, size_t keep_history, uint64_t empty_ttl_ms, size_t max_rooms,
        size_t max_users);

    void set_sender(SendFn fn) { send_ = std::move(fn); }

    void on_text(int fd, const std::string& payload);
    void on_disconnect(int fd);
    void tick(uint64_t now);
    void shutdown();


    std::string history_json(const std::string& room_id, size_t limit) const;
    size_t room_count() const { return rooms_.size(); }
    size_t active_rooms() const;
    size_t online() const { return where_.size(); }

    // ---- 管理接口（只给本机 admin socket 用）----
    struct RoomInfo {
        std::string id;
        size_t users = 0;
        size_t msgs = 0;
        std::string names;
    };
    std::vector<RoomInfo> rooms_info() const;
    std::vector<std::string> users_of(const std::string& room_id) const;
    std::string history_text(const std::string& room_id, size_t limit) const;
    int find_user(const std::string& room_id, const std::string& name) const;
    int kick_user(const std::string& room_id, const std::string& name);
    std::vector<int> close_room(const std::string& room_id);
    void send_to(int fd, const std::string& json) { send(fd, json); }

private:
    struct Rate {
        uint64_t window = 0;
        unsigned count = 0;
    };


    void handle_create(int fd, const std::string& js);
    void handle_join(int fd, const std::string& js);
    void handle_msg(int fd, const std::string& js);
    void handle_leave(int fd);

    void join(int fd, const std::string& room_id, const std::string& want_name, bool created);
    void leave(int fd, bool announce);

    void broadcast(Room& r, const std::string& json, int except_fd = -1);
    void send(int fd, const std::string& json);
    void send_error(int fd, const std::string& text);
    std::string next_room_id();
    Room* room_of(int fd);

    std::string dir_;
    size_t keep_;
    uint64_t empty_ttl_ms_;
    size_t max_rooms_;
    size_t max_users_ = 30;
    std::map<std::string, std::unique_ptr<Room>> rooms_;
    std::unordered_map<int, std::string> where_;  // fd -> room id
    std::unordered_map<int, Rate> rate_;
    SendFn send_;
    long next_uid_ = 1;
};

}  // namespace lt
