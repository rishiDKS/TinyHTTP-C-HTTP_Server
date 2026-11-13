#include "server.hpp"
#include "http.hpp"
#include "thread_pool.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace {
volatile std::sig_atomic_t running = 1;
void stop_server(int) { running = 0; }
bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) { if (errno == EINTR) continue; return false; }
        if (n == 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}
std::string mime_type(const fs::path& path) {
    auto ext = path.extension().string();
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}
}
HttpServer::HttpServer(std::string host, int port, std::string root, std::size_t workers)
    : host_(std::move(host)), root_(fs::absolute(root).lexically_normal().string()), port_(port), worker_count_(workers) {}
int HttpServer::create_listener() const {

    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_PASSIVE;
    addrinfo* result = nullptr;
    const std::string service = std::to_string(port_);

    int rc = getaddrinfo(host_.empty() ? nullptr : host_.c_str(), service.c_str(), &hints, &result);

    if (rc != 0) { std::cerr << "getaddrinfo: " << gai_strerror(rc) << '\n'; return -1; }

    int listener = -1;

    for (auto* p = result; p; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) continue;
        int yes = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(listener, p->ai_addr, p->ai_addrlen) == 0 && listen(listener, 128) == 0) break;
        close(listener); listener = -1;
    }

    freeaddrinfo(result);

    if (listener < 0) std::cerr << "Could not bind/listen: " << std::strerror(errno) << '\n';
    return listener;
}
int HttpServer::run() {
    int listener = create_listener(); if (listener < 0) return 1;

    std::signal(SIGINT, stop_server); std::signal(SIGTERM, stop_server);

    ThreadPool pool(worker_count_, worker_count_ * 8);

    std::cout << "TinyHTTP listening on " << (host_.empty() ? "0.0.0.0" : host_) << ':' << port_
              << " | root=" << root_ << " | workers=" << worker_count_ << std::endl;

    while (running) {
        sockaddr_storage addr{}; socklen_t len = sizeof(addr);
        int client = accept(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        if (client < 0) { if (errno == EINTR) continue; if (!running) break; std::cerr << "accept: " << strerror(errno) << '\n'; continue; }
        if (!pool.submit([this, client] { handle_client(client); })) close(client);
    }

    close(listener);
    std::cout << "Server stopped. Draining worker queue...\n";
    return 0;
}
void HttpServer::handle_client(int fd) {
    constexpr std::size_t MAX_HEADER = 16 * 1024;
    std::string raw; raw.reserve(2048); char buf[2048];

    while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < MAX_HEADER) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0) { if (errno == EINTR) continue; close(fd); return; }
        if (n == 0) { close(fd); return; }
        raw.append(buf, static_cast<std::size_t>(n));
    }

    HttpResponse response; HttpRequest request; std::string error;
    
    if (raw.size() >= MAX_HEADER && raw.find("\r\n\r\n") == std::string::npos) {
        response = make_error(431, "Request Header Fields Too Large", "Request headers exceed 16 KiB");
    } else if (!parse_request(raw, request, error)) {
        response = make_error(400, "Bad Request", error);
    } else if (request.method != "GET" && request.method != "HEAD") {
        response = make_error(405, "Method Not Allowed", "TinyHTTP supports GET and HEAD");
    } else {
        // Strip query string; decode is deliberately omitted to avoid ambiguous path handling.
        std::string path = request.target.substr(0, request.target.find('?'));
        if (path.find('%') != std::string::npos || path.find('\\') != std::string::npos) {
            response = make_error(400, "Bad Request", "Encoded or backslash paths are not supported");
        } else {
            fs::path relative = fs::path(path).relative_path();
            fs::path candidate = (fs::path(root_) / relative).lexically_normal();
            // Lexical containment check prevents ../ traversal outside document root.
            const std::string root_prefix = fs::path(root_).lexically_normal().string();
            const std::string candidate_string = candidate.string();
            const bool contained = candidate_string == root_prefix ||
                (candidate_string.size() > root_prefix.size() &&
                 candidate_string.compare(0, root_prefix.size(), root_prefix) == 0 &&
                 candidate_string[root_prefix.size()] == '/');
            if (!contained) {
                response = make_error(403, "Forbidden", "Path escapes document root");
            } else {
                if (path == "/") candidate /= "index.html";
                std::error_code ec;
                if (fs::is_directory(candidate, ec)) candidate /= "index.html";
                // Resolve symlinks too: lexical checks alone can be bypassed by a link inside www.
                const fs::path resolved = fs::weakly_canonical(candidate, ec);
                const std::string resolved_string = ec ? candidate.string() : resolved.string();
                const bool resolved_inside = resolved_string == root_prefix ||
                    (resolved_string.size() > root_prefix.size() &&
                     resolved_string.compare(0, root_prefix.size(), root_prefix) == 0 &&
                     resolved_string[root_prefix.size()] == '/');
                if (!resolved_inside) {
                    response = make_error(403, "Forbidden", "Resolved path escapes document root");
                } else {
                std::ifstream file(candidate, std::ios::binary);
                if (!file) response = make_error(404, "Not Found", "Resource not found");
                else {
                    response.body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                    response.content_type = mime_type(candidate);
                }
                }
            }
        }
    }
    const bool head = request.method == "HEAD";
    if (!send_all(fd, response.serialize(head))) std::cerr << "send failed\n";
    std::cout << "[" << response.status << "] " << (request.method.empty() ? "?" : request.method)
              << ' ' << (request.target.empty() ? "?" : request.target) << '\n';
    close(fd);
}
