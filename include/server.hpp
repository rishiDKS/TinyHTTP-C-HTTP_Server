#pragma once
#include <string>
class ThreadPool;
class HttpServer {
public:
    HttpServer(std::string host, int port, std::string root, std::size_t workers);
    int run();
private:
    void handle_client(int client_fd);
    int create_listener() const;
    std::string host_, root_;
    int port_;
    std::size_t worker_count_;
};
