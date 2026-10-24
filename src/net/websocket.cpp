#include "websocket.h"

#include <cstring>

#include "../util/base64.h"
#include "../util/sha1.h"

namespace lt {

std::string ws_accept(const std::string& key) {
    static const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    return b64_encode(sha1_raw(key + kGuid));
}

std::string ws_frame(int opcode, const std::string& payload) {
    std::string o;
    size_t n = payload.size();
    o.reserve(n + 10);
    o += static_cast<char>(0x80 | (opcode & 0x0F));
    if (n < 126) {
        o += static_cast<char>(n);
    } else if (n <= 0xFFFF) {
        o += static_cast<char>(126);
        o += static_cast<char>((n >> 8) & 0xFF);
        o += static_cast<char>(n & 0xFF);
    } else {
        o += static_cast<char>(127);
        for (int i = 7; i >= 0; --i)
            o += static_cast<char>((static_cast<uint64_t>(n) >> (8 * i)) & 0xFF);
    }
    o += payload;
    return o;
}

std::string ws_close(int code, const std::string& reason) {
    std::string p;
    p += static_cast<char>((code >> 8) & 0xFF);
    p += static_cast<char>(code & 0xFF);
    p += reason;
    return ws_frame(WS_CLOSE, p);
}

}  // namespace lt
