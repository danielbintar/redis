#include <csignal>
#include <cstdlib>
#include <iostream>

#include "server.h"

static Server* g_server = nullptr;

static void onSignal(int) {
    std::cout << "\nShutting down...\n";
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int         port = 6379;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc)
            port = std::atoi(argv[++i]);
        else if (arg == "--host" && i + 1 < argc)
            host = argv[++i];
    }

    try {
        Server server(host, port);
        g_server = &server;

        std::signal(SIGINT, onSignal);
        std::signal(SIGTERM, onSignal);

        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
