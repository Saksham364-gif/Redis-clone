#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int MAX_ARRAY_COUNT = 1024;
const int MAX_BULK_LEN = 512 * 1024 * 1024;
const int MAX_LINE_LEN = 64;

std::unordered_map<std::string, std::string> store;
std::unordered_map<std::string, std::deque<std::string>> listStore;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hashStore;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiryTimes;

using Clock = std::chrono::steady_clock;

bool isExpired(const std::string& key) {
    auto it = expiryTimes.find(key);
    if (it == expiryTimes.end()) return false;
    if (Clock::now() >= it->second) {
        store.erase(key);
        listStore.erase(key);
        hashStore.erase(key);
        expiryTimes.erase(it);
        return true;
    }
    return false;
}

void clearExpiry(const std::string& key) {
    expiryTimes.erase(key);
}

struct ParseResult {
    std::vector<std::string> args;
    bool disconnected = false;
    bool protocolError = false;
    std::string errorMsg;
};

ParseResult parseCommand(int client_fd) {
    ParseResult result;

    auto readByte = [&](char& out) -> bool {
        ssize_t n = read(client_fd, &out, 1);
        return n == 1;
    };

    auto readLine = [&](std::string& out) -> bool {
        char ch;
        out.clear();
        while (readByte(ch)) {
            if (ch == '\r') {
                if (!readByte(ch) || ch != '\n') return false;
                return true;
            }
            out += ch;
            if (out.size() > MAX_LINE_LEN) return false;
        }
        return false;
    };

    auto parseIntStrict = [](const std::string& s, long long& out) -> bool {
        if (s.empty()) return false;
        size_t i = 0;
        bool neg = false;
        if (s[0] == '-') { neg = true; i = 1; }
        if (i >= s.size()) return false;
        long long val = 0;
        for (; i < s.size(); i++) {
            if (!isdigit((unsigned char)s[i])) return false;
            val = val * 10 + (s[i] - '0');
        }
        out = neg ? -val : val;
        return true;
    };

    char c;
    if (!readByte(c)) { result.disconnected = true; return result; }

    if (c != '*') {
        result.protocolError = true;
        result.errorMsg = "expected array";
        return result;
    }

    std::string countLine;
    if (!readLine(countLine)) { result.disconnected = true; return result; }

    long long count;
    if (!parseIntStrict(countLine, count) || count < 0 || count > MAX_ARRAY_COUNT) {
        result.protocolError = true;
        result.errorMsg = "invalid multibulk length";
        return result;
    }

    for (long long i = 0; i < count; i++) {
        char typeChar;
        if (!readByte(typeChar)) { result.disconnected = true; return result; }
        if (typeChar != '$') {
            result.protocolError = true;
            result.errorMsg = "expected bulk string";
            return result;
        }

        std::string lenLine;
        if (!readLine(lenLine)) { result.disconnected = true; return result; }

        long long len;
        if (!parseIntStrict(lenLine, len) || len < 0 || len > MAX_BULK_LEN) {
            result.protocolError = true;
            result.errorMsg = "invalid bulk length";
            return result;
        }

        std::string value;
        value.resize(len);
        long long totalRead = 0;
        while (totalRead < len) {
            ssize_t n = read(client_fd, &value[totalRead], len - totalRead);
            if (n <= 0) { result.disconnected = true; return result; }
            totalRead += n;
        }

        char cr, lf;
        if (!readByte(cr) || !readByte(lf) || cr != '\r' || lf != '\n') {
            result.protocolError = true;
            result.errorMsg = "expected CRLF after bulk string";
            return result;
        }

        result.args.push_back(value);
    }

    return result;
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

void sendArrayHeader(int fd, int count) {
    std::string resp = "*" + std::to_string(count) + "\r\n";
    write(fd, resp.c_str(), resp.size());
}

void handleCommand(int client_fd, std::vector<std::string>& args) {
    if (args.empty()) return;

    std::string cmd = args[0];
    for (auto& ch : cmd) ch = toupper(ch);

    if (cmd != "EXISTS" && args.size() >= 2) {
        isExpired(args[1]);
    }

    if (cmd == "PING") {
        sendSimpleString(client_fd, "PONG");
    }
    else if (cmd == "ECHO" && args.size() >= 2) {
        sendBulkString(client_fd, args[1]);
    }
    else if (cmd == "SET" && args.size() >= 3) {
        store[args[1]] = args[2];
        clearExpiry(args[1]);
        sendSimpleString(client_fd, "OK");
    }
    else if (cmd == "GET" && args.size() >= 2) {
        auto it = store.find(args[1]);
        if (it == store.end()) sendNull(client_fd);
        else sendBulkString(client_fd, it->second);
    }
    else if (cmd == "DEL" && args.size() >= 2) {
        int deleted = store.erase(args[1]);
        deleted += listStore.erase(args[1]);
        deleted += hashStore.erase(args[1]);
        clearExpiry(args[1]);
        sendInteger(client_fd, deleted);
    }
    else if (cmd == "EXPIRE" && args.size() >= 3) {
        bool exists = store.count(args[1]) || listStore.count(args[1]) || hashStore.count(args[1]);
        if (!exists) { sendInteger(client_fd, 0); return; }
        long long seconds;
        try { seconds = std::stoll(args[2]); }
        catch (...) { sendError(client_fd, "value is not an integer or out of range"); return; }
        expiryTimes[args[1]] = Clock::now() + std::chrono::seconds(seconds);
        sendInteger(client_fd, 1);
    }
    else if (cmd == "TTL" && args.size() >= 2) {
        bool exists = store.count(args[1]) || listStore.count(args[1]) || hashStore.count(args[1]);
        if (!exists) { sendInteger(client_fd, -2); return; }
        auto it = expiryTimes.find(args[1]);
        if (it == expiryTimes.end()) { sendInteger(client_fd, -1); return; }
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second - Clock::now()).count();
        sendInteger(client_fd, remaining > 0 ? remaining : 0);
    }
    else if (cmd == "PERSIST" && args.size() >= 2) {
        int removed = expiryTimes.erase(args[1]);
        sendInteger(client_fd, removed);
    }
    else if (cmd == "EXISTS" && args.size() >= 2) {
        isExpired(args[1]);
        bool exists = store.count(args[1]) || listStore.count(args[1]) || hashStore.count(args[1]);
        sendInteger(client_fd, exists ? 1 : 0);
    }
    else if (cmd == "LPUSH" && args.size() >= 3) {
        for (size_t i = 2; i < args.size(); i++) {
            listStore[args[1]].push_front(args[i]);
        }
        sendInteger(client_fd, listStore[args[1]].size());
    }
    else if (cmd == "RPUSH" && args.size() >= 3) {
        for (size_t i = 2; i < args.size(); i++) {
            listStore[args[1]].push_back(args[i]);
        }
        sendInteger(client_fd, listStore[args[1]].size());
    }
    else if (cmd == "LLEN" && args.size() >= 2) {
        auto it = listStore.find(args[1]);
        sendInteger(client_fd, it == listStore.end() ? 0 : it->second.size());
    }
    else if (cmd == "LRANGE" && args.size() >= 4) {
        auto it = listStore.find(args[1]);
        if (it == listStore.end()) {
            sendArrayHeader(client_fd, 0);
            return;
        }
        auto& lst = it->second;
        int len = lst.size();
        int start, stop;
        try {
            start = std::stoi(args[2]);
            stop = std::stoi(args[3]);
        } catch (...) {
            sendError(client_fd, "value is not an integer or out of range");
            return;
        }
        if (start < 0) start = std::max(0, len + start);
        if (stop < 0) stop = len + stop;
        if (stop >= len) stop = len - 1;
        if (start > stop || len == 0) {
            sendArrayHeader(client_fd, 0);
            return;
        }
        sendArrayHeader(client_fd, stop - start + 1);
        for (int i = start; i <= stop; i++) {
            sendBulkString(client_fd, lst[i]);
        }
    }
    else if (cmd == "HSET" && args.size() >= 4) {
        int added = 0;
        for (size_t i = 2; i + 1 < args.size(); i += 2) {
            if (hashStore[args[1]].find(args[i]) == hashStore[args[1]].end()) added++;
            hashStore[args[1]][args[i]] = args[i + 1];
        }
        sendInteger(client_fd, added);
    }
    else if (cmd == "HGET" && args.size() >= 3) {
        auto it = hashStore.find(args[1]);
        if (it == hashStore.end()) { sendNull(client_fd); return; }
        auto fit = it->second.find(args[2]);
        if (fit == it->second.end()) sendNull(client_fd);
        else sendBulkString(client_fd, fit->second);
    }
    else if (cmd == "HGETALL" && args.size() >= 2) {
        auto it = hashStore.find(args[1]);
        if (it == hashStore.end()) {
            sendArrayHeader(client_fd, 0);
            return;
        }
        sendArrayHeader(client_fd, it->second.size() * 2);
        for (auto& kv : it->second) {
            sendBulkString(client_fd, kv.first);
            sendBulkString(client_fd, kv.second);
        }
    }
    else if (cmd == "HDEL" && args.size() >= 3) {
        auto it = hashStore.find(args[1]);
        if (it == hashStore.end()) { sendInteger(client_fd, 0); return; }
        int deleted = it->second.erase(args[2]);
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
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(6380);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n"; return 1;
    }
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n"; return 1;
    }

    std::cout << "my-redis listening on 127.0.0.1:6380...\n";

    while (true) {
        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd < 0) { std::cerr << "Accept failed\n"; continue; }

        std::cout << "Client connected: " << inet_ntoa(client_address.sin_addr) << "\n";

        while (true) {
            ParseResult result = parseCommand(client_fd);

            if (result.disconnected) {
                std::cout << "Client disconnected\n";
                break;
            }

            if (result.protocolError) {
                sendError(client_fd, "Protocol error: " + result.errorMsg);
                break;
            }

            handleCommand(client_fd, result.args);
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}