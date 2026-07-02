#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "server.h"

namespace {
Server* g_server = nullptr;

// Parse an integer CLI argument, exiting with a clear message on bad input.
int64_t parseIntArg(const char* flag, const char* value) {
    try {
        return std::stoll(value);
    } catch (const std::logic_error&) {
        std::cerr << "Invalid value for " << flag << ": '" << value << "' is not an integer\n";
        std::exit(1);
    }
}
}  // namespace

// Signal handlers must have C linkage (per the C++ standard) and may only use
// async-signal-safe operations: write() and lock-free atomics — no std::cout,
// no locking, no heap allocation.
extern "C" void onSignal(int /*sig*/) {
    constexpr char kMsg[] = "\nShutting down...\n";
    (void)!write(STDERR_FILENO, kMsg, sizeof(kMsg) - 1);
    if (g_server != nullptr) g_server->stop();
}

int main(int argc, char* argv[]) {
    std::string host      = "127.0.0.1";
    int         port      = 6379;
    int64_t     maxMemory = 0;  // 0 = unlimited

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<int>(parseIntArg("--port", argv[++i]));
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--maxmemory" && i + 1 < argc) {
            maxMemory = parseIntArg("--maxmemory", argv[++i]);
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            return 1;
        }
    }

    try {
        Server server(host, port, maxMemory);
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
