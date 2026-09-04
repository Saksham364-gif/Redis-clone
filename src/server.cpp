#include "resp.h"
#include "storage.h"
#include "commands.h"
#include "persistence.h"
#include <iostream>
#include <cstdlib>
#include <unordered_map>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

const int MAX_EVENTS = 1024;
const int READ_CHUNK = 65536;

std::unordered_map<int, ClientState> clients;

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char* argv[]) {
    const char* envPass = std::getenv("REDIS_PASSWORD");
    if (envPass != nullptr) {
        requirePass = envPass;
    }

    std::string bindAddr = "127.0.0.1";
    const char* envBind = std::getenv("REDIS_BIND");
    if (envBind != nullptr) {
        bindAddr = envBind;
    }

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--requirepass" && i + 1 < argc) {
            requirePass = argv[i + 1];
        }
        if (std::string(argv[i]) == "--bind" && i + 1 < argc) {
            bindAddr = argv[i + 1];
        }
    }

    loadAOF();
    openAOF();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { std::cerr << "Failed to create socket\n"; return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(bindAddr.c_str());
    address.sin_port = htons(6380);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n"; return 1;
    }
    if (listen(server_fd, 128) < 0) {
        std::cerr << "Listen failed\n"; return 1;
    }
    setNonBlocking(server_fd);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) { std::cerr << "epoll_create1 failed\n"; return 1; }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    std::cout << "my-redis (epoll) listening on " << bindAddr << ":6380..." << (requirePass.empty() ? " (no auth)" : " (auth required)") << "\n";

    std::vector<epoll_event> events(MAX_EVENTS);

    while (true) {
        int n = epoll_wait(epoll_fd, events.data(), MAX_EVENTS, -1);
        if (n < 0) continue;

        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if (client_fd < 0) break;

                    setNonBlocking(client_fd);
                    int one = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

                    epoll_event cev{};
                    cev.events = EPOLLIN;
                    cev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &cev);

                    ClientState newClient;
                    newClient.authenticated = requirePass.empty();
                    clients[client_fd] = newClient;
                }
                continue;
            }

            auto cit = clients.find(fd);
            if (cit == clients.end()) continue;
            ClientState& state = cit->second;

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                clients.erase(fd);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                char buf[READ_CHUNK];
                bool shouldClose = false;
                while (true) {
                    ssize_t r = read(fd, buf, sizeof(buf));
                    if (r > 0) {
                        state.inbuf.append(buf, r);
                        if (r < (ssize_t)sizeof(buf)) break;
                    } else if (r == 0) {
                        shouldClose = true;
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        shouldClose = true;
                        break;
                    }
                }

                while (true) {
                    size_t consumed;
                    std::vector<std::string> args;
                    std::string errMsg;
                    ParseStatus status = tryParseCommand(state.inbuf, consumed, args, errMsg);

                    if (status == ParseStatus::INCOMPLETE) break;

                    if (status == ParseStatus::ERROR) {
                        appendError(state.outbuf, "Protocol error: " + errMsg);
                        shouldClose = true;
                        break;
                    }

                    handleCommand(state.outbuf, args, state);
                    state.inbuf.erase(0, consumed);
                }

                if (!state.outbuf.empty()) {
                    ssize_t w = write(fd, state.outbuf.data(), state.outbuf.size());
                    if (w > 0) state.outbuf.erase(0, w);
                }

                if (shouldClose) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    clients.erase(fd);
                }
            }
        }
    }

    close(server_fd);
    return 0;
}