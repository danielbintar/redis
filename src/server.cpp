#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "commands.h"
#include "resp.h"

Server::Server(std::string host, int port) : host_(std::move(host)), port_(port) { setupSocket(); }

Server::~Server() { stop(); }

void Server::setupSocket() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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
}

void Server::handleClient(int fd) {
    // disable Nagle for lower latency
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    std::string            buf;
    std::array<char, 4096> tmp{};

    while (running_) {
        ssize_t n = recv(fd, tmp.data(), tmp.size(), 0);
        if (n <= 0) break;
        buf.append(tmp.data(), static_cast<size_t>(n));

        while (true) {
            auto tokens = resp::parse(buf);
            if (!tokens) break;
            if (tokens->empty()) continue;

            std::string response = handleCommand(store_, *tokens);

            // QUIT closes the connection after sending OK
            bool quit = (!tokens->empty() && ((*tokens)[0] == "QUIT" || (*tokens)[0] == "quit"));

            ssize_t sent = 0;
            while (std::cmp_less(sent, response.size())) {
                ssize_t r = send(fd, response.data() + sent, response.size() - sent, 0);
                if (r <= 0) goto disconnect;
                sent += r;
            }
            if (quit) goto disconnect;
        }
    }
disconnect:
    close(fd);
}

void Server::run() {
    running_ = true;
    std::cout << "Redis server listening on " << host_ << ":" << port_ << "\n";

    while (running_) {
        sockaddr_in client{};
        socklen_t   len = sizeof(client);
        int         fd  = accept(listenFd_, reinterpret_cast<sockaddr*>(&client), &len);
        if (fd < 0) break;

        std::array<char, INET_ADDRSTRLEN> ip{};
        inet_ntop(AF_INET, &client.sin_addr, ip.data(), ip.size());
        std::cout << "Client connected: " << ip.data() << ":" << ntohs(client.sin_port) << "\n";

        std::thread([this, fd]() { handleClient(fd); }).detach();
    }
}
