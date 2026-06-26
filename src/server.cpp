#include "server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#ifdef __APPLE__
#include <sys/event.h>
#else
#include <sys/epoll.h>
#endif

#include <array>
#include <iostream>
#include <stdexcept>

#include "commands.h"
#include "resp.h"

// ── socket helpers ────────────────────────────────────────────────────────────

static void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void applyClientOpts(int fd) {
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
#ifdef __APPLE__
    int idle = 60;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle));
#else
    int idle = 60;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
    int intvl = 10;
    int cnt   = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
}

// ── lifecycle ─────────────────────────────────────────────────────────────────

Server::Server(std::string host, int port, int64_t maxMemory)
    : host_(std::move(host)), port_(port) {
    store_.setMaxMemory(maxMemory);
    setupSocket();
#ifdef __APPLE__
    eventFd_ = kqueue();
    if (eventFd_ < 0) throw std::runtime_error("kqueue() failed");
#else
    eventFd_ = epoll_create1(0);
    if (eventFd_ < 0) throw std::runtime_error("epoll_create1() failed");
#endif
    watchRead(listenFd_);
}

Server::~Server() { stop(); }

void Server::setupSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(listenFd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed on " + host_ + ":" + std::to_string(port_));
    if (listen(listenFd_, SOMAXCONN) < 0) throw std::runtime_error("listen() failed");
}

void Server::stop() {
    running_ = false;
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
    if (eventFd_ >= 0) {
        close(eventFd_);
        eventFd_ = -1;
    }
}

// ── event registration ────────────────────────────────────────────────────────

void Server::watchRead(int fd) const {
#ifdef __APPLE__
    struct kevent ev{};
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    kevent(eventFd_, &ev, 1, nullptr, 0, nullptr);
#else
    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(eventFd_, EPOLL_CTL_ADD, fd, &ev);
#endif
}

void Server::watchWrite(int fd, bool enable) const {
#ifdef __APPLE__
    struct kevent ev{};
    EV_SET(&ev, fd, EVFILT_WRITE, enable ? (EV_ADD | EV_ENABLE) : (EV_ADD | EV_DISABLE), 0, 0,
           nullptr);
    kevent(eventFd_, &ev, 1, nullptr, 0, nullptr);
#else
    struct epoll_event ev{};
    ev.events  = EPOLLIN | (enable ? EPOLLOUT : 0);
    ev.data.fd = fd;
    epoll_ctl(eventFd_, EPOLL_CTL_MOD, fd, &ev);
#endif
}

// ── client management ─────────────────────────────────────────────────────────

void Server::acceptClient() {
    while (true) {
        sockaddr_in addr{};
        socklen_t   len = sizeof(addr);
        int         fd  = accept(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len);
        if (fd < 0) break;  // EAGAIN — no more pending connections

        setNonBlocking(fd);
        applyClientOpts(fd);
        watchRead(fd);
        clients_.emplace(fd, Client{.fd = fd, .rbuf = {}, .wbuf = {}});

        std::array<char, INET_ADDRSTRLEN> ip{};
        inet_ntop(AF_INET, &addr.sin_addr, ip.data(), ip.size());
        std::cout << "Client connected: " << ip.data() << ":" << ntohs(addr.sin_port) << "\n";
    }
}

void Server::closeClient(int fd) {
#ifndef __APPLE__
    epoll_ctl(eventFd_, EPOLL_CTL_DEL, fd, nullptr);
#endif
    close(fd);
    clients_.erase(fd);
}

void Server::readClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Client& client = it->second;

    // drain the socket into rbuf
    std::array<char, 4096> tmp{};
    while (true) {
        ssize_t n = recv(fd, tmp.data(), tmp.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeClient(fd);
            return;
        }
        if (n == 0) {
            closeClient(fd);
            return;
        }
        client.rbuf.append(tmp.data(), static_cast<size_t>(n));
    }

    // parse and dispatch all complete commands
    while (true) {
        auto tokens = resp::parse(client.rbuf);
        if (!tokens) break;
        if (tokens->empty()) continue;

        const std::string& cmd  = (*tokens)[0];
        bool               quit = (cmd == "QUIT" || cmd == "quit");

        client.wbuf += handleCommand(store_, *tokens);
        if (quit) {
            client.closing = true;
            break;
        }
    }

    // flush whatever we can right now
    if (!client.wbuf.empty()) writeClient(fd);
}

void Server::writeClient(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) return;
    Client& client = it->second;

    while (!client.wbuf.empty()) {
        ssize_t n = send(fd, client.wbuf.data(), client.wbuf.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                watchWrite(fd, true);  // ask to be notified when writable again
                return;
            }
            closeClient(fd);
            return;
        }
        client.wbuf.erase(0, static_cast<size_t>(n));
    }

    watchWrite(fd, false);  // nothing left to write

    if (client.closing) closeClient(fd);
}

// ── event loop ────────────────────────────────────────────────────────────────

void Server::run() {
    running_ = true;
    std::cout << "Redis server listening on " << host_ << ":" << port_ << "\n";

#ifdef __APPLE__
    std::array<struct kevent, 64> events{};
    while (running_) {
        int n =
            kevent(eventFd_, nullptr, 0, events.data(), static_cast<int>(events.size()), nullptr);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            auto fd = static_cast<int>(events[i].ident);
            if (((events[i].flags & EV_EOF) != 0U) || ((events[i].flags & EV_ERROR) != 0U)) {
                if (fd != listenFd_) closeClient(fd);
                continue;
            }
            if (fd == listenFd_) {
                acceptClient();
            } else if (events[i].filter == EVFILT_READ) {
                readClient(fd);
            } else if (events[i].filter == EVFILT_WRITE) {
                writeClient(fd);
            }
        }
    }
#else
    std::array<struct epoll_event, 64> events{};
    while (running_) {
        int n = epoll_wait(eventFd_, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                if (fd != listenFd_) closeClient(fd);
                continue;
            }
            if (fd == listenFd_) {
                acceptClient();
            } else {
                if (events[i].events & EPOLLIN) readClient(fd);
                if ((events[i].events & EPOLLOUT) && clients_.count(fd)) writeClient(fd);
            }
        }
    }
#endif
}
