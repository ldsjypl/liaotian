// text.h - small string helpers shared by the http and chat layers.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "time.h"

namespace lt {

inline std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (unsigned char)s[b] <= ' ') ++b;
    while (e > b && (unsigned char)s[e - 1] <= ' ') --e;
    return s.substr(b, e - b);
}

inline std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

inline bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && std::memcmp(s.data(), p.data(), p.size()) == 0;
}

inline bool is_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

// Drops control characters and clamps to max_bytes, cutting on a UTF-8 boundary.
inline std::string sanitize_text(const std::string& s, size_t max_bytes) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if (c < 0x20 && c != '\n' && c != '\t') continue;
        if (c == 0x7f) continue;
        o += char(c);
    }
    if (o.size() > max_bytes) {
        o.resize(max_bytes);
        while (!o.empty() && (static_cast<unsigned char>(o.back()) & 0xC0) == 0x80) o.pop_back();
    }
    return o;
}

// Uniform random decimal digits; the first digit is never 0.
inline std::string random_digits(size_t n) {
    if (n == 0) return std::string();
    std::string out;
    out.reserve(n);
    FILE* f = std::fopen("/dev/urandom", "rb");
    unsigned char buf[64] = {0};
    size_t got = 0;
    if (f) {
        got = std::fread(buf, 1, sizeof(buf), f);
        std::fclose(f);
    }
    if (got < n) {
        // Deterministic fallback so room creation never fails outright.
        uint64_t seed = mono_ms();
        for (size_t i = 0; i < n; ++i) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            buf[i] = static_cast<unsigned char>(seed >> 33);
        }
    }
    for (size_t i = 0; i < n; ++i) {
        unsigned v = buf[i] % 10;
        if (i == 0 && v == 0) v = 1 + (buf[i] % 9);
        out += static_cast<char>('0' + v);
    }
    return out;
}

}  // namespace lt
