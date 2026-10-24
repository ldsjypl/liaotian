// websocket.h - minimal RFC 6455 server-side framing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace lt {

enum WsOpcode {
    WS_CONT  = 0x0,
    WS_TEXT  = 0x1,
    WS_BIN   = 0x2,
    WS_CLOSE = 0x8,
    WS_PING  = 0x9,
    WS_PONG  = 0xA,
};

struct WsFrame {
    int opcode = 0;
    bool fin = true;
    std::string payload;
};

class WsParser {
public:
    static const uint64_t kMaxFrame = 1u << 20;

    void feed(const char* p, size_t n) { buf_.append(p, n); }
    size_t buffered() const { return buf_.size(); }

    // 1 = frame ready, 0 = need more bytes, -1 = protocol error (err filled).
    int next(WsFrame& out, std::string& err) {
        if (buf_.size() < 2) return 0;
        const unsigned char* d = reinterpret_cast<const unsigned char*>(buf_.data());
        int b0 = d[0], b1 = d[1];
        bool fin = (b0 & 0x80) != 0;
        int op = b0 & 0x0F;
        bool masked = (b1 & 0x80) != 0;
        uint64_t len = b1 & 0x7F;
        size_t off = 2;

        if (len == 126) {
            if (buf_.size() < 4) return 0;
            len = (uint64_t(d[2]) << 8) | d[3];
            off = 4;
        } else if (len == 127) {
            if (buf_.size() < 10) return 0;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | d[2 + i];
            off = 10;
        }
        if (len > kMaxFrame) {
            err = "frame too large";
            return -1;
        }
        size_t need = off + (masked ? 4 : 0) + static_cast<size_t>(len);
        if (buf_.size() < need) return 0;

        std::string payload;
        if (masked) {
            unsigned char m[4];
            std::memcpy(m, d + off, 4);
            payload.resize(static_cast<size_t>(len));
            for (size_t i = 0; i < len; ++i)
                payload[i] = static_cast<char>(d[off + 4 + i] ^ m[i % 4]);
        } else {
            payload.assign(reinterpret_cast<const char*>(d + off), static_cast<size_t>(len));
        }
        buf_.erase(0, need);
        out.opcode = op;
        out.fin = fin;
        out.payload.swap(payload);
        return 1;
    }

private:
    std::string buf_;
};

std::string ws_accept(const std::string& client_key);
std::string ws_frame(int opcode, const std::string& payload);
std::string ws_close(int code, const std::string& reason);

}  // namespace lt
