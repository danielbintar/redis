#pragma once
#include <atomic>
#include <string>
#include <unordered_map>

#include "store.h"

struct Client {
    int         fd;
    std::string rbuf;            // accumulated incoming bytes
    std::string wbuf;            // pending outgoing bytes
    bool        closing{false};  // send wbuf then close
};

class Server {
public:
    Server(std::string host, int port);
    ~Server();

    void run();  // blocks; single-threaded event loop
    void stop();

private:
    std::string                     host_;
    int                             port_;
    int                             listenFd_{-1};
    int                             eventFd_{-1};  // kqueue (macOS) or epoll (Linux) fd
    std::atomic<bool>               running_{false};
    Store                           store_;
    std::unordered_map<int, Client> clients_;

    void setupSocket();
    void watchRead(int fd) const;
    void watchWrite(int fd, bool enable) const;
    void acceptClient();
    void readClient(int fd);
    void writeClient(int fd);
    void closeClient(int fd);
};
