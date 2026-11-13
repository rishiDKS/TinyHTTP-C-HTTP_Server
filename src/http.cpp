#include "http.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace {
std::string trim(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}
bool parse_request(const std::string& raw, HttpRequest& req, std::string& error) {
    const auto end = raw.find("\r\n");
    if (end == std::string::npos) { error = "Malformed request line"; return false; }
    std::istringstream line(raw.substr(0, end));
    if (!(line >> req.method >> req.target >> req.version)) { error = "Malformed request line"; return false; }
    std::string extra;
    if (line >> extra) { error = "Too many request-line fields"; return false; }
    if (req.version != "HTTP/1.0" && req.version != "HTTP/1.1") { error = "Unsupported HTTP version"; return false; }
    std::size_t pos = end + 2;
    while (pos < raw.size()) {
        auto next = raw.find("\r\n", pos);
        if (next == std::string::npos) { error = "Headers are not CRLF terminated"; return false; }
        if (next == pos) break;
        std::string header = raw.substr(pos, next - pos);
        auto colon = header.find(':');
        if (colon == std::string::npos || colon == 0) { error = "Malformed header"; return false; }
        std::string key = lower(trim(header.substr(0, colon)));
        std::string value = trim(header.substr(colon + 1));
        if (req.headers.count(key)) { error = "Duplicate header"; return false; }
        req.headers.emplace(std::move(key), std::move(value));
        pos = next + 2;
    }
    if (req.target.empty() || req.target[0] != '/') { error = "Only origin-form targets are supported"; return false; }
    return true;
}
std::string HttpResponse::serialize(bool head_only) const {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Server: TinyHTTP/1.0\r\n\r\n";
    if (!head_only) out << body;
    return out.str();
}
HttpResponse make_error(int code, std::string reason, std::string message) {
    HttpResponse r; r.status = code; r.reason = std::move(reason);
    r.content_type = "text/plain; charset=utf-8"; r.body = std::move(message) + "\n";
    return r;
}
