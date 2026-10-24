#include "hub.h"

#include <cstdio>
#include <ctime>
#include <deque>
#include <vector>

#include "../util/json.h"
#include "../util/text.h"
#include "../util/time.h"

namespace lt {

namespace {
constexpr size_t kMaxRooms = 200;
constexpr size_t kMaxNameBytes = 24;
constexpr size_t kMaxMsgBytes = 2000;
constexpr int kRateCount = 5;
constexpr int kRateWindowMs = 3000;
constexpr size_t kHistoryInWelcome = 100;
}  // namespace

Hub::Hub(std::string dir, size_t keep_history, uint64_t empty_ttl_ms, size_t max_rooms,
         size_t max_users)
    : dir_(std::move(dir)), keep_(keep_history), empty_ttl_ms_(empty_ttl_ms),
      max_rooms_(max_rooms ? max_rooms : kMaxRooms), max_users_(max_users ? max_users : 30) {}

std::string Hub::next_room_id() {
    for (int i = 0; i < 200; ++i) {
        std::string id = random_digits(6);
        if (!rooms_.count(id)) return id;
    }
    return std::string();
}

Room* Hub::room_of(int fd) {
    auto it = where_.find(fd);
    if (it == where_.end()) return nullptr;
    auto r = rooms_.find(it->second);
    return r == rooms_.end() ? nullptr : r->second.get();
}

void Hub::send(int fd, const std::string& json) {
    if (send_) send_(fd, json);
}

void Hub::send_error(int fd, const std::string& text) {
    send(fd, "{\"type\":\"error\",\"text\":\"" + json_escape(text) + "\"}");
}

void Hub::broadcast(Room& r, const std::string& json, int except_fd) {

    for (const auto& kv : where_) {
        if (kv.second != r.id()) continue;
        if (kv.first == except_fd) continue;
        send(kv.first, json);
    }
}

void Hub::on_text(int fd, const std::string& payload) {
    std::string type;
    if (!json_get_string(payload, "type", type)) {
        send_error(fd, "缺少 type 字段");
        return;
    }
    if (type == "create") {
        handle_create(fd, payload);
    } else if (type == "join") {
        handle_join(fd, payload);
    } else if (type == "msg") {
        handle_msg(fd, payload);
    } else if (type == "leave") {
        handle_leave(fd);
    } else if (type == "ping") {
        send(fd, "{\"type\":\"pong\"}");
    } else {
        send_error(fd, "未知的 type");
    }
}

void Hub::handle_create(int fd, const std::string& js) {
    if (rooms_.size() >= max_rooms_) {
        send_error(fd, "房间数量已达上限，稍后再试");
        return;
    }
    std::string name;
    json_get_string(js, "name", name);
    std::string id = next_room_id();
    if (id.empty()) {
        send_error(fd, "无法分配房间号");
        return;
    }
    auto room = std::make_unique<Room>(id, dir_, keep_);
    if (!room->init()) {
        send_error(fd, "房间初始化失败");
        return;
    }
    rooms_[id] = std::move(room);
    std::fprintf(stderr, "room created: %s (by fd=%d)\n", id.c_str(), fd);
    join(fd, id, name, true);
}

void Hub::handle_join(int fd, const std::string& js) {
    std::string id, name;
    json_get_string(js, "room", id);
    json_get_string(js, "name", name);
    if (!is_digits(id) || id.size() != 6) {
        send_error(fd, "房间号应为 6 位数字");
        return;
    }
    if (!rooms_.count(id)) {
        send_error(fd, "房间不存在，可能已经解散");
        return;
    }
    join(fd, id, name, false);
}

void Hub::join(int fd, const std::string& room_id, const std::string& want_name, bool created) {
    if (where_.count(fd)) leave(fd, true);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        send_error(fd, "房间不存在");
        return;
    }
    Room& r = *it->second;
    if (r.user_count() >= max_users_) {
        send_error(fd, "房间人数已满（上限 " + std::to_string(max_users_) + " 人）");
        return;
    }

    std::string name = trim(sanitize_text(want_name, kMaxNameBytes));
    if (name.empty()) name = "游客";

    std::string final_name;
    if (!r.add_member(fd, next_uid_++, name, final_name)) {
        send_error(fd, "加入房间失败");
        return;
    }
    where_[fd] = room_id;

    std::string out = "{\"type\":\"joined\",\"room\":\"" + room_id +
                      "\",\"name\":\"" + json_escape(final_name) +
                      "\",\"created\":" + (created ? "true" : "false") +
                      ",\"users\":" + r.users_json() +
                      ",\"history\":" + r.history_json(kHistoryInWelcome) + "}";
    send(fd, out);

    std::string sys = "{\"type\":\"sys\",\"room\":\"" + room_id +
                      "\",\"text\":\"" + json_escape(final_name + " 加入了聊天") +
                      "\",\"ts\":" + std::to_string(wall_sec()) + "}";
    broadcast(r, sys, fd);
    broadcast(r, "{\"type\":\"users\",\"room\":\"" + room_id + "\",\"list\":" + r.users_json() + "}", fd);
    std::fprintf(stderr, "join fd=%d room=%s name=%s users=%zu\n", fd, room_id.c_str(),
                 final_name.c_str(), r.user_count());
}

void Hub::handle_msg(int fd, const std::string& js) {
    Room* r = room_of(fd);
    if (!r) {
        send_error(fd, "请先进入房间");
        return;
    }

    uint64_t now = mono_ms();
    Rate& rt = rate_[fd];
    if (rt.window == 0 || now - rt.window > static_cast<uint64_t>(kRateWindowMs)) {
        rt.window = now;
        rt.count = 0;
    }
    if (rt.count >= static_cast<unsigned>(kRateCount)) {
        send_error(fd, "发言太快了，歇一下");
        return;
    }

    std::string text;
    if (!json_get_string(js, "text", text)) return;
    text = trim(sanitize_text(text, kMaxMsgBytes));
    if (text.empty()) return;

    ++rt.count;
    int64_t ts = wall_sec();
    std::string from = r->name_of(fd);
    long id = r->append_message(from, text, ts);

    std::string out = "{\"type\":\"msg\",\"room\":\"" + r->id() +
                      "\",\"id\":" + std::to_string(id) +
                      ",\"from\":\"" + json_escape(from) +
                      "\",\"text\":\"" + json_escape(text) +
                      "\",\"ts\":" + std::to_string(ts) + "}";
    broadcast(*r, out);
}

void Hub::handle_leave(int fd) {
    if (!where_.count(fd)) {
        send(fd, "{\"type\":\"left\"}");
        return;
    }
    leave(fd, true);
    send(fd, "{\"type\":\"left\"}");
}

void Hub::leave(int fd, bool announce) {
    auto it = where_.find(fd);
    if (it == where_.end()) return;
    std::string room_id = it->second;
    where_.erase(it);

    auto r = rooms_.find(room_id);
    if (r == rooms_.end()) return;
    Room& room = *r->second;
    std::string name = room.name_of(fd);
    room.remove_member(fd);

    if (announce && !name.empty()) {
        std::string sys = "{\"type\":\"sys\",\"room\":\"" + room_id +
                          "\",\"text\":\"" + json_escape(name + " 离开了聊天") +
                          "\",\"ts\":" + std::to_string(wall_sec()) + "}";
        broadcast(room, sys);
        broadcast(room, "{\"type\":\"users\",\"room\":\"" + room_id + "\",\"list\":" +
                            room.users_json() + "}");
    }
    if (room.empty()) room.note_empty(mono_ms());
    std::fprintf(stderr, "leave fd=%d room=%s name=%s users=%zu\n", fd, room_id.c_str(),
                 name.c_str(), room.user_count());
}

void Hub::on_disconnect(int fd) {
    leave(fd, true);
    rate_.erase(fd);
}

void Hub::tick(uint64_t now) {
    std::vector<std::string> dead;
    for (auto& kv : rooms_) {
        Room& r = *kv.second;
        if (!r.empty()) {
            r.clear_empty();
            continue;
        }
        if (r.empty_since() == 0) {
            r.note_empty(now);
            continue;
        }
        if (now - r.empty_since() > empty_ttl_ms_) dead.push_back(kv.first);
    }
    for (const std::string& id : dead) {
        auto it = rooms_.find(id);
        if (it == rooms_.end()) continue;
        it->second->destroy();
        rooms_.erase(it);
        std::fprintf(stderr, "room reaped: %s (empty for %llu ms)\n", id.c_str(),
                     static_cast<unsigned long long>(empty_ttl_ms_));
    }
}

void Hub::shutdown() {
    for (auto& kv : rooms_) kv.second->close();
    rooms_.clear();
    where_.clear();
}

size_t Hub::active_rooms() const {
    size_t n = 0;
    for (const auto& kv : rooms_) if (kv.second->user_count() > 0) ++n;
    return n;
}
std::vector<Hub::RoomInfo> Hub::rooms_info() const {
    std::vector<RoomInfo> out;
    for (const auto& kv : rooms_) {
        const Room& r = *kv.second;
        RoomInfo info;
        info.id = r.id();
        info.users = r.user_count();
        info.msgs = r.msg_count();
        bool first = true;
        for (const std::string& n : r.names()) {
            if (!first) info.names += ", ";
            info.names += n;
            first = false;
        }
        if (info.names.empty()) info.names = "-";
        out.push_back(std::move(info));
    }
    return out;
}

std::vector<std::string> Hub::users_of(const std::string& room_id) const {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return {};
    return it->second->names();
}

std::string Hub::history_text(const std::string& room_id, size_t limit) const {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return "房间不存在\n";
    const std::deque<Msg>& msgs = it->second->messages();
    size_t start = msgs.size() > limit ? msgs.size() - limit : 0;
    std::string out;
    char stamp[32];
    for (size_t i = start; i < msgs.size(); ++i) {
        time_t t = static_cast<time_t>(msgs[i].ts);
        struct tm tmv {};
        localtime_r(&t, &tmv);
        std::strftime(stamp, sizeof(stamp), "%m-%d %H:%M", &tmv);
        out += std::string("[") + stamp + "] " + msgs[i].from + ": " + msgs[i].text + "\n";
    }
    if (out.empty()) out = "(没有消息)\n";
    return out;
}

int Hub::find_user(const std::string& room_id, const std::string& name) const {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return -1;
    return it->second->fd_of(name);
}

int Hub::kick_user(const std::string& room_id, const std::string& name) {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return -1;
    Room& r = *it->second;
    int fd = r.fd_of(name);
    if (fd < 0) return -1;
    send(fd, "{\"type\":\"error\",\"text\":\"你已被管理员移出房间\"}");
    leave(fd, true);
    return fd;
}

std::vector<int> Hub::close_room(const std::string& room_id) {
    std::vector<int> fds;
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return fds;
    Room& r = *it->second;
    fds = r.member_fds();
    std::string notice = "{\"type\":\"closed\",\"room\":\"" + room_id + "\"}";
    for (int fd : fds) {
        send(fd, notice);
        where_.erase(fd);
        rate_.erase(fd);
    }
    r.destroy();
    rooms_.erase(it);
    std::fprintf(stderr, "admin closed room %s (%zu users)\n", room_id.c_str(), fds.size());
    return fds;
}

std::string Hub::history_json(const std::string& room_id, size_t limit) const {
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return "[]";
    return it->second->history_json(limit);
}

}  // namespace lt
