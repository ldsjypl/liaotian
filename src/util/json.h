// json.h - the tiny subset of JSON we need: escaping and flat field extraction.
#pragma once

#include <cstdio>
#include <string>

namespace lt {

inline std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o;
}

// Reads a string field from a flat object. Handles \" \\ \/ \n \r \t \b \f and
// \uXXXX (BMP only). Returns false when the key is absent or not a string.
inline bool json_get_string(const std::string& js, const std::string& key, std::string& out) {
    const std::string pat = "\"" + key + "\"";
    size_t k = js.find(pat);
    if (k == std::string::npos) return false;
    size_t i = js.find(':', k + pat.size());
    if (i == std::string::npos) return false;
    ++i;
    while (i < js.size() && (js[i] == ' ' || js[i] == '\t' || js[i] == '\n' || js[i] == '\r')) ++i;
    if (i >= js.size() || js[i] != '"') return false;
    ++i;
    out.clear();
    while (i < js.size()) {
        char c = js[i++];
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }
        if (i >= js.size()) return false;
        char e = js[i++];
        switch (e) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'u': {
                if (i + 4 > js.size()) return false;
                unsigned v = 0;
                for (int n = 0; n < 4; ++n) {
                    char h = js[i++];
                    v <<= 4;
                    if (h >= '0' && h <= '9') v |= unsigned(h - '0');
                    else if (h >= 'a' && h <= 'f') v |= unsigned(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') v |= unsigned(h - 'A' + 10);
                    else return false;
                }
                if (v < 0x80) {
                    out += char(v);
                } else if (v < 0x800) {
                    out += char(0xC0 | (v >> 6));
                    out += char(0x80 | (v & 0x3F));
                } else {
                    out += char(0xE0 | (v >> 12));
                    out += char(0x80 | ((v >> 6) & 0x3F));
                    out += char(0x80 | (v & 0x3F));
                }
                break;
            }
            default: return false;
        }
    }
    return false;
}

}  // namespace lt
