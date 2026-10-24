// message_log.h - append-only JSONL log plus an in-memory ring of recent msgs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace lt {

struct Msg {
    long id = 0;
    std::string from;
    std::string text;
    int64_t ts = 0;
};

class MessageLog {
public:
    ~MessageLog() { close(); }

    bool open(const std::string& path, size_t keep);
    long append(const std::string& from, const std::string& text, int64_t ts);

    const std::deque<Msg>& recent() const { return msgs_; }
    std::string json(size_t limit) const;
    size_t size() const { return msgs_.size(); }
    long last_id() const { return next_id_ - 1; }

    void sync_now();
    void close();
    void close_and_remove();

private:
    bool load();
    void maybe_sync(bool force);

    std::string path_;
    std::deque<Msg> msgs_;
    size_t keep_ = 200;
    long next_id_ = 1;
    int fd_ = -1;
    int unsynced_ = 0;
    uint64_t last_sync_ms_ = 0;
};

}  // namespace lt
