#include "http.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>

#include "../util/text.h"

namespace lt {


bool http_parse(const std::string& head, HttpRequest& out) {
    size_t eol = head.find("\r\n");
    if (eol == std::string::npos) return false;
    std::string line = head.substr(0, eol);

    size_t s1 = line.find(' ');
    size_t s2 = (s1 == std::string::npos) ? std::string::npos : line.find(' ', s1 + 1);
    if (s1 == std::string::npos) return false;

    out.method = line.substr(0, s1);
    out.target = line.substr(s1 + 1, (s2 == std::string::npos ? line.size() : s2) - s1 - 1);

    size_t q = out.target.find('?');
    if (q == std::string::npos) {
        out.path = out.target;
    } else {
        out.path = out.target.substr(0, q);
        out.query = out.target.substr(q + 1);
    }

    size_t pos = eol + 2;
    while (pos < head.size()) {
        size_t next = head.find("\r\n", pos);
        if (next == std::string::npos) break;
        if (next == pos) break;  // end of headers
        std::string hline = head.substr(pos, next - pos);
        size_t colon = hline.find(':');
        if (colon != std::string::npos) {
            std::string key = lower(trim(hline.substr(0, colon)));
            std::string val = trim(hline.substr(colon + 1));
            out.headers[key] = val;
        }
        pos = next + 2;
    }
    return true;
}

bool http_header(const HttpRequest& req, const std::string& name, std::string& out) {
    auto it = req.headers.find(lower(name));
    if (it == req.headers.end()) return false;
    out = it->second;
    return true;
}

const char* http_reason(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        default: return "Error";
    }
}

std::string http_response(int code, const std::string& ctype, const std::string& body,
                          bool keep_alive) {
    std::string out = "HTTP/1.1 " + std::to_string(code) + " " + http_reason(code) + "\r\n";
    out += "Server: liaotian\r\n";
    out += "Content-Type: " + ctype + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    out += "\r\n";
    out += body;
    return out;
}

std::string mime_for(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string ext = lower(path.substr(dot));
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

bool read_file(const std::string& path, std::string& out, size_t max_bytes) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (static_cast<size_t>(st.st_size) > max_bytes) return false;

    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    out.resize(static_cast<size_t>(st.st_size));
    size_t off = 0;
    while (off < out.size()) {
        ssize_t n = ::read(fd, &out[off], out.size() - off);
        if (n <= 0) break;
        off += static_cast<size_t>(n);
    }
    ::close(fd);
    out.resize(off);
    return true;
}

bool safe_rel_path(const std::string& p) {
    if (p.empty() || p[0] != '/') return false;
    if (p.find("..") != std::string::npos) return false;
    for (char c : p) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '/' || c == '.' || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::string url_decode(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                o += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') {
            o += ' ';
            continue;
        }
        o += s[i];
    }
    return o;
}

std::string query_param(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        size_t eq = pair.find('=');
        if (eq != std::string::npos && pair.substr(0, eq) == key)
            return url_decode(pair.substr(eq + 1));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return std::string();
}

}  // namespace lt
