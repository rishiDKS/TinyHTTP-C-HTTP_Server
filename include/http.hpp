#pragma once
#include <string>
#include <unordered_map>

struct HttpRequest {
    std::string method, target, version;
    std::unordered_map<std::string, std::string> headers;
};
struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
    std::string serialize(bool head_only = false) const;
};
bool parse_request(const std::string& raw, HttpRequest& req, std::string& error);

HttpResponse make_error(int code, std::string reason, std::string message);
