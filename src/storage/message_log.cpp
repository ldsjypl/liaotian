#include "message_log.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "../util/json.h"
#include "../util/time.h"

namespace lt {

namespace {

bool line_get_num(const std::string& js, const char* key, long long& out) {
    const std::string pat = std::string("\"") + key + "\"";
    size_t k = js.find(pat);
    if (k == std::string::npos) return false;
    size_t i = js.find(':', k + pat.size());
    if (i == std::string::npos) return false;
    ++i;
    while (i < js.size() && (js[i] == ' ' || js[i] == '\t')) ++i;
    bool neg = false;
    if (i < js.size() && js[i] == '-') { neg = true; ++i; }
    long long v = 0;
    bool any = false;
    while (i < js.size() && js[i] >= '0' && js[i] <= '9') {
        v = v * 10 + (js[i] - '0');
        ++i;
        any = true;
    }
    if (!any) return false;
    out = neg ? -v : v;
    return true;
}

}  // namespace

bool MessageLog::open(const std::string& path, size_t keep) {
    path_ = path;
    keep_ = keep ? keep : 200;
    load();
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd_ < 0) {
        std::fprintf(stderr, "message_log: cannot open %s: %s\n", path_.c_str(), std::strerror(errno));
        return false;
    }
    last_sync_ms_ = mono_ms();
    return true;
}

bool MessageLog::load() {
    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) return false;

    std::string line;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), f)) {
        line.assign(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (line.size() < 8 || line[0] != '{') continue;  // torn tail after a power cut
        Msg m;
        long long id = 0, ts = 0;
        if (!line_get_num(line, "id", id)) continue;
        if (!json_get_string(line, "from", m.from)) continue;
        if (!json_get_string(line, "text", m.text)) continue;
        line_get_num(line, "ts", ts);
        m.id = static_cast<long>(id);
        m.ts = ts;
        msgs_.push_back(m);
        if (m.id >= next_id_) next_id_ = m.id + 1;
        while (msgs_.size() > keep_) msgs_.pop_front();
    }
    std::fclose(f);
    return true;
}

long MessageLog::append(const std::string& from, const std::string& text, int64_t ts) {
    Msg m;
    m.id = next_id_++;
    m.from = from;
    m.text = text;
    m.ts = ts;

    std::string line = "{\"id\":" + std::to_string(m.id) +
                       ",\"from\":\"" + json_escape(m.from) +
                       "\",\"text\":\"" + json_escape(m.text) +
                       "\",\"ts\":" + std::to_string(m.ts) + "}\n";
    if (fd_ >= 0) {
        size_t off = 0;
        while (off < line.size()) {
            ssize_t n = ::write(fd_, line.data() + off, line.size() - off);
            if (n <= 0) {
                if (errno == EINTR) continue;
                std::fprintf(stderr, "message_log: write failed: %s\n", std::strerror(errno));
                break;
            }
            off += static_cast<size_t>(n);
        }
        ++unsynced_;
    }

    msgs_.push_back(m);
    while (msgs_.size() > keep_) msgs_.pop_front();
    maybe_sync(false);
    return m.id;
}

void MessageLog::maybe_sync(bool force) {
    uint64_t now = mono_ms();
    bool by_count = unsynced_ >= 50;
    bool by_time = (now - last_sync_ms_) >= 10000 && unsynced_ > 0;
    if (!force && !by_count && !by_time) return;
    if (fd_ >= 0) ::fdatasync(fd_);
    unsynced_ = 0;
    last_sync_ms_ = now;
}

void MessageLog::sync_now() { maybe_sync(true); }

void MessageLog::close() {
    if (fd_ >= 0) {
        maybe_sync(true);
        ::close(fd_);
        fd_ = -1;
    }
}

void MessageLog::close_and_remove() {
    close();
    if (!path_.empty()) ::unlink(path_.c_str());
}

std::string MessageLog::json(size_t limit) const {
    if (limit == 0 || limit > msgs_.size()) limit = msgs_.size();
    std::string out = "[";
    size_t start = msgs_.size() - limit;
    for (size_t i = start; i < msgs_.size(); ++i) {
        const Msg& m = msgs_[i];
        if (i > start) out += ",";
        out += "{\"id\":" + std::to_string(m.id) +
               ",\"from\":\"" + json_escape(m.from) +
               "\",\"text\":\"" + json_escape(m.text) +
               "\",\"ts\":" + std::to_string(m.ts) + "}";
    }
    out += "]";
    return out;
}

}  // namespace lt
