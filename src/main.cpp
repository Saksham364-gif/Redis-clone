#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

std::unordered_map<std::string, std::string> store;

std::vector<std::string> parseCommand(int client_fd) {
    std::vector<std::string> args;
    char c;

    auto readByte = [&](char& out) -> bool {
        ssize_t n = read(client_fd, &out, 1);
        return n == 1;
    };

    auto readLine = [&]() -> std::string {
        std::string line;
        char ch;
        while (readByte(ch)) {
            if (ch == '\r') {
                readByte(ch);
                break;
            }
            line += ch;
        }
        return line;
    };

    if (!readByte(c)) return args;

    if (c != '*') {
        readLine();
        return args;
    }

    std::string countStr = readLine();
    int count = std::stoi(countStr);

    for (int i = 0; i < count; i++) {
        char typeChar;
        if (!readByte(typeChar) || typeChar != '$') break;

        std::string lenStr = readLine();
        int len = std::stoi(lenStr);

        std::string value;
        value.resize(len);
        int totalRead = 0;
        while (totalRead < len) {
            ssize_t n = read(client_fd, &value[totalRead], len - totalRead);
            if (n <= 0) break;
            totalRead += n;
        }
        char cr, lf;
        readByte(cr); readByte(lf);

        args.push_back(value);
    }

    return args;
}

void sendSimpleString(int fd, const std::string& s) {
    std::string resp = "+" + s + "\r\n";
    write(fd, resp.c_str(), resp.size());
}

void sendError(int fd, const std::string& s) {
    std::string resp = "-ERR " + s + "\r\n";
    write(fd, resp.c_str(), resp.size());
}

void sendBulkString(int fd, const std::string& s) {
    std::string resp = "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
    write(fd, resp.c_str(), resp.size());
}

void sendNull(int fd) {
    std::string resp = "$-1\r\n";
    write(fd, resp.c_str(), resp.size());
}

void sendInteger(int fd, long long val) {
    std::string resp = ":" + std::to_string(val) + "\r\n";
    write(fd, resp.c_str(), resp.size());
}

void handleCommand(int client_fd, std::vector<std::string>& args) {
    if (args.empty()) return;

    std::string cmd = args[0];
    for (auto& ch : cmd) ch = toupper(ch);

    if (cmd == "PING") {
        sendSimpleString(client_fd, "PONG");
    }
    else if (cmd == "ECHO" && args.size() >= 2) {
        sendBulkString(client_fd, args[1]);
    }
    else if (cmd == "SET" && args.size() >= 3) {
        store[args[1]] = args[2];
        sendSimpleString(client_fd, "OK");
    }
    else if (cmd == "GET" && args.size() >= 2) {
        auto it = store.find(args[1]);
        if (it == store.end()) sendNull(client_fd);
        else sendBulkString(client_fd, it->second);
    }
    else if (cmd == "DEL" && args.size() >= 2) {
        int deleted = store.erase(args[1]);
        sendInteger(client_fd, deleted);
    }
    else {
        sendError(client_fd, "unknown command '" + cmd + "'");
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { std::cerr << "Failed to create socket\n"; return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(6380);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n"; return 1;
    }
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n"; return 1;
    }

    std::cout << "my-redis listening on port 6380...\n";

    while (true) {
        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd < 0) { std::cerr << "Accept failed\n"; continue; }

        std::cout << "Client connected: " << inet_ntoa(client_address.sin_addr) << "\n";

        while (true) {
            std::vector<std::string> args = parseCommand(client_fd);
            if (args.empty()) {
                std::cout << "Client disconnected\n";
                break;
            }

            std::cout << "Command:";
            for (auto& a : args) std::cout << " " << a;
            std::cout << "\n";

            handleCommand(client_fd, args);
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}