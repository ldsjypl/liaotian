// room.h - one chat room: members, recent history and its own message log.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../storage/message_log.h"

namespace lt {

class Room {
public:
    Room(std::string id, std::string dir, size_t keep_history);

    bool init();
    const std::string& id() const { return id_; }
    const std::string& path() const { return path_; }

    // Adds a member, making the nickname unique inside the room.
    bool add_member(int fd, long uid, const std::string& wanted, std::string& out_name);
    bool remove_member(int fd);
    bool has_member(int fd) const;
    std::string name_of(int fd) const;
    int fd_of(const std::string& name) const;
    std::vector<int> member_fds() const;
    std::vector<std::string> names() const;

    bool empty() const { return members_.empty(); }
    size_t user_count() const { return members_.size(); }
    std::string users_json() const;
    std::string meta_json() const;

    long append_message(const std::string& from, const std::string& text, int64_t ts);
    const std::deque<Msg>& messages() const { return log_.recent(); }
    size_t msg_count() const { return log_.size(); }
    std::string history_json(size_t limit) const { return log_.json(limit); }

    // Empty-room bookkeeping used by the reaper.
    uint64_t empty_since() const { return empty_since_; }
    void note_empty(uint64_t now) { if (!empty_since_) empty_since_ = now; }
    void clear_empty() { empty_since_ = 0; }

    void close();    // flush and close the history file, keep it
    void destroy();  // closes and unlinks the history file

private:
    struct Member {
        int fd;
        long uid;
        std::string name;
    };

    std::string unique_name(const std::string& wanted) const;

    std::string id_;
    std::string dir_;
    std::string path_;
    std::vector<Member> members_;
    MessageLog log_;
    size_t log_keep_ = 200;
    uint64_t empty_since_ = 0;
};

}  // namespace lt
