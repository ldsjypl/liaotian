// http.h - request parsing, response building and static file serving.
#pragma once

#include <cstddef>
#include <map>
#include <string>

namespace lt {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;  // lower-case keys
};

// `head` is everything up to and including the blank line.
bool http_parse(const std::string& head, HttpRequest& out);

bool http_header(const HttpRequest& req, const std::string& name, std::string& out);

const char* http_reason(int code);
std::string http_response(int code, const std::string& ctype, const std::string& body,
                          bool keep_alive);

std::string mime_for(const std::string& path);
bool read_file(const std::string& path, std::string& out, size_t max_bytes);

bool safe_rel_path(const std::string& p);
std::string url_decode(const std::string& s);
std::string query_param(const std::string& query, const std::string& key);

}  // namespace lt
