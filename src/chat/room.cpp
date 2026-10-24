#include "room.h"

#include <cstdio>

#include "../util/json.h"

namespace lt {

Room::Room(std::string id, std::string dir, size_t keep_history)
    : id_(std::move(id)), dir_(std::move(dir)) {
    path_ = dir_ + "/" + id_ + ".jsonl";
    (void)keep_history;
    log_keep_ = keep_history;
}

bool Room::init() { return log_.open(path_, log_keep_); }

std::string Room::unique_name(const std::string& wanted) const {
    auto taken = [this](const std::string& n) {
        for (const Member& m : members_)
            if (m.name == n) return true;
        return false;
    };
    if (!taken(wanted)) return wanted;
    for (int i = 2; i < 100; ++i) {
        std::string cand = wanted + "#" + std::to_string(i);
        if (!taken(cand)) return cand;
    }
    return wanted + "#" + std::to_string(members_.size() + 1);
}

bool Room::add_member(int fd, long uid, const std::string& wanted, std::string& out_name) {
    for (const Member& m : members_)
        if (m.fd == fd) return false;
    out_name = unique_name(wanted);
    members_.push_back(Member{fd, uid, out_name});
    clear_empty();
    return true;
}

bool Room::remove_member(int fd) {
    for (auto it = members_.begin(); it != members_.end(); ++it) {
        if (it->fd == fd) {
            members_.erase(it);
            return true;
        }
    }
    return false;
}

bool Room::has_member(int fd) const {
    for (const Member& m : members_)
        if (m.fd == fd) return true;
    return false;
}

std::string Room::name_of(int fd) const {
    for (const Member& m : members_)
        if (m.fd == fd) return m.name;
    return std::string();
}

std::vector<std::string> Room::names() const {
    std::vector<std::string> out;
    out.reserve(members_.size());
    for (const Member& m : members_) out.push_back(m.name);
    return out;
}

int Room::fd_of(const std::string& name) const {
    for (const Member& m : members_)
        if (m.name == name) return m.fd;
    return -1;
}

std::vector<int> Room::member_fds() const {
    std::vector<int> out;
    out.reserve(members_.size());
    for (const Member& m : members_) out.push_back(m.fd);
    return out;
}

std::string Room::users_json() const {
    std::string out = "[";
    for (size_t i = 0; i < members_.size(); ++i) {
        if (i) out += ",";
        out += "\"" + json_escape(members_[i].name) + "\"";
    }
    out += "]";
    return out;
}

std::string Room::meta_json() const {
    return "{\"id\":\"" + id_ + "\",\"users\":" + std::to_string(members_.size()) + "}";
}

long Room::append_message(const std::string& from, const std::string& text, int64_t ts) {
    return log_.append(from, text, ts);
}

void Room::close() { log_.close(); }
void Room::destroy() {
    log_.close_and_remove();
    std::fprintf(stderr, "room destroyed: %s\n", id_.c_str());
}

}  // namespace lt
