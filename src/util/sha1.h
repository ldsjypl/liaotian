// sha1.h - SHA-1, only needed for the websocket accept key.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace lt {

class Sha1 {
public:
    Sha1() { reset(); }

    void reset() {
        h_[0] = 0x67452301u; h_[1] = 0xEFCDAB89u; h_[2] = 0x98BADCFEu;
        h_[3] = 0x10325476u; h_[4] = 0xC3D2E1F0u;
        total_ = 0; fill_ = 0;
    }

    void update(const void* data, size_t n) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        total_ += n;
        while (n > 0) {
            size_t take = 64 - fill_;
            if (take > n) take = n;
            std::memcpy(buf_ + fill_, p, take);
            fill_ += take; p += take; n -= take;
            if (fill_ == 64) { block(buf_); fill_ = 0; }
        }
    }

    void final(unsigned char out[20]) {
        uint64_t bits = total_ * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (fill_ != 56) update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
        update(len, 8);
        for (int i = 0; i < 5; ++i) {
            out[i * 4 + 0] = static_cast<unsigned char>(h_[i] >> 24);
            out[i * 4 + 1] = static_cast<unsigned char>(h_[i] >> 16);
            out[i * 4 + 2] = static_cast<unsigned char>(h_[i] >> 8);
            out[i * 4 + 3] = static_cast<unsigned char>(h_[i]);
        }
    }

private:
    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    void block(const unsigned char* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) |
                   (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e;
    }

    uint32_t h_[5];
    uint64_t total_;
    unsigned char buf_[64];
    size_t fill_;
};

inline std::string sha1_raw(const std::string& s) {
    Sha1 h;
    h.update(s.data(), s.size());
    unsigned char out[20];
    h.final(out);
    return std::string(reinterpret_cast<char*>(out), 20);
}

}  // namespace lt
