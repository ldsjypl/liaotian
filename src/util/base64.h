// base64.h - standard base64 encoding (used by the websocket handshake).
#pragma once

#include <cstdint>
#include <string>

namespace lt {

inline std::string b64_encode(const std::string& in) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(in.data());
    while (i + 2 < in.size()) {
        uint32_t n = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8) | p[i + 2];
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += tbl[n & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        uint32_t n = uint32_t(p[i]) << 16;
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t n = (uint32_t(p[i]) << 16) | (uint32_t(p[i + 1]) << 8);
        out += tbl[(n >> 18) & 63];
        out += tbl[(n >> 12) & 63];
        out += tbl[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

}  // namespace lt
