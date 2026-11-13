#include "server.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string host = "0.0.0.0", root = "www";
    int port = 8080; std::size_t workers = 4;
    // Usage: tinyhttp [port] [document-root] [worker-count] [bind-address]
    if (argc > 1) port = std::stoi(argv[1]);
    if (argc > 2) root = argv[2];
    if (argc > 3) workers = std::stoul(argv[3]);
    if (argc > 4) host = argv[4];
    if (port < 1 || port > 65535 || workers == 0) {
        std::cerr << "Usage: " << argv[0] << " [port 1-65535] [document-root] [workers>0] [bind-address]\n";
        return 2;
    }
    return HttpServer(host, port, root, workers).run();
}
