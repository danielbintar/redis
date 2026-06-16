#pragma once
#include <atomic>
#include <string>

#include "store.h"

class Server {
public:
    Server(std::string host, int port);
    ~Server();

    void run();  // blocks; accepts connections and spawns threads
    void stop();

private:
    std::string       host_;
    int               port_;
    int               listenFd_{-1};
    std::atomic<bool> running_{false};
    Store             store_;

    void handleClient(int fd);
    void setupSocket();
};
