#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <deque>
#include <set>
#include <unordered_map>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

const int MAX_ARRAY_COUNT = 1024;
const int MAX_BULK_LEN = 512 * 1024 * 1024;
const int MAX_LINE_LEN = 64;
const int MAX_EVENTS = 1024;
const int READ_CHUNK = 65536;
const std::string AOF_FILE = "appendonly.aof";

std::unordered_map<std::string, std::string> store;
std::unordered_map<std::string, std::deque<std::string>> listStore;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hashStore;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiryTimes;

std::string requirePass = "";
std::ofstream aofStream;

const std::set<std::string> WRITE_COMMANDS = {
    "SET", "DEL", "LPUSH", "RPUSH", "HSET", "HDEL", "EXPIRE", "PERSIST"
};

using Clock = std::chrono::steady_clock;

struct ClientState {
    std::string inbuf;
    std::string outbuf;
    bool authenticated = false;
};

std::unordered_map<int, ClientState> clients;

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

bool parseIntStrict(const std::string& s, long long& out) {
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
}

enum class ParseStatus { OK, INCOMPLETE, ERROR };

ParseStatus tryParseCommand(const std::string& buf, size_t& consumed, std::vector<std::string>& args, std::string& errMsg) {
    args.clear();
    size_t pos = 0;

    if (buf.empty()) return ParseStatus::INCOMPLETE;
    if (buf[pos] != '*') { errMsg = "expected array"; return ParseStatus::ERROR; }
    pos++;

    size_t lineEnd = buf.find("\r\n", pos);
    if (lineEnd == std::string::npos) {
        if (buf.size() - pos > MAX_LINE_LEN) { errMsg = "invalid multibulk length"; return ParseStatus::ERROR; }
        return ParseStatus::INCOMPLETE;
    }
    std::string countStr = buf.substr(pos, lineEnd - pos);
    long long count;
    if (!parseIntStrict(countStr, count) || count < 0 || count > MAX_ARRAY_COUNT) {
        errMsg = "invalid multibulk length";
        return ParseStatus::ERROR;
    }
    pos = lineEnd + 2;

    for (long long i = 0; i < count; i++) {
        if (pos >= buf.size()) return ParseStatus::INCOMPLETE;
        if (buf[pos] != '$') { errMsg = "expected bulk string"; return ParseStatus::ERROR; }
        pos++;

        lineEnd = buf.find("\r\n", pos);
        if (lineEnd == std::string::npos) {
            if (buf.size() - pos > MAX_LINE_LEN) { errMsg = "invalid bulk length"; return ParseStatus::ERROR; }
            return ParseStatus::INCOMPLETE;
        }
        std::string lenStr = buf.substr(pos, lineEnd - pos);
        long long len;
        if (!parseIntStrict(lenStr, len) || len < 0 || len > MAX_BULK_LEN) {
            errMsg = "invalid bulk length";
            return ParseStatus::ERROR;
        }
        pos = lineEnd + 2;

        if (pos + (size_t)len + 2 > buf.size()) return ParseStatus::INCOMPLETE;

        std::string value = buf.substr(pos, len);
        pos += len;

        if (buf[pos] != '\r' || buf[pos + 1] != '\n') {
            errMsg = "expected CRLF after bulk string";
            return ParseStatus::ERROR;
        }
        pos += 2;

        args.push_back(value);
    }

    consumed = pos;
    return ParseStatus::OK;
}

std::string encodeCommand(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (auto& a : args) {
        out += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
    }
    return out;
}

void appendSimpleString(std::string& out, const std::string& s) { out += "+" + s + "\r\n"; }
void appendError(std::string& out, const std::string& s) { out += "-ERR " + s + "\r\n"; }
void appendBulkString(std::string& out, const std::string& s) { out += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n"; }
void appendNull(std::string& out) { out += "$-1\r\n"; }
void appendInteger(std::string& out, long long val) { out += ":" + std::to_string(val) + "\r\n"; }
void appendArrayHeader(std::string& out, int count) { out += "*" + std::to_string(count) + "\r\n"; }

void writeToAOF(std::vector<std::string>& args) {
    if (!aofStream.is_open()) return;
    aofStream << encodeCommand(args);
    aofStream.flush();
}

void handleCommand(std::string& out, std::vector<std::string>& args, ClientState& client, bool fromAOF = false) {
    if (args.empty()) return;

    std::string cmd = args[0];
    for (auto& ch : cmd) ch = toupper(ch);

    if (!fromAOF) {
        if (!requirePass.empty() && !client.authenticated) {
            if (cmd == "AUTH") {
                if (args.size() >= 2 && args[1] == requirePass) {
                    client.authenticated = true;
                    appendSimpleString(out, "OK");
                } else {
                    appendError(out, "invalid password");
                }
                return;
            }
            if (cmd == "QUIT") { appendSimpleString(out, "OK"); return; }
            appendError(out, "NOAUTH Authentication required.");
            return;
        }
        if (cmd == "AUTH") {
            appendError(out, "Client sent AUTH, but no password is set.");
            return;
        }
    }

    if (cmd != "EXISTS" && args.size() >= 2) {
        isExpired(args[1]);
    }

    if (!fromAOF && WRITE_COMMANDS.count(cmd)) {
        writeToAOF(args);
    }

    if (cmd == "PING") {
        appendSimpleString(out, "PONG");
    }
    else if (cmd == "ECHO" && args.size() >= 2) {
        appendBulkString(out, args[1]);
    }
    else if (cmd == "SET" && args.size() >= 3) {
        store[args[1]] = args[2];
        clearExpiry(args[1]);
        appendSimpleString(out, "OK");
    }
    else if (cmd == "GET" && args.size() >= 2) {
        auto it = store.find(args[1]);
        if (it == store.end()) appendNull(out);
        else appendBulkString(out, it->second);
    }
    else if (cmd == "DEL" && args.size() >= 2) {
        int deleted = store.erase(args[1]);
        deleted += listStore.erase(args[1]);
        deleted += hashStore.erase(args[1]);
        clearExpiry(args[1]);
        appendInteger(out, deleted);
    }
    else if (cmd == "EXPIRE" && args.size() >= 3) {
        bool exists = store.count(args[1]) || listStore.count(args[1]) || hashStore.count(args[1]);
        if (!exists) { appendInteger(out, 0); return; }
        long long seconds;
        if (!parseIntStrict(args[2], seconds)) { appendError(out, "value is not an integer or out of range"); return; }
        expiryTimes[args[1]] = Clock::now() + std::chrono::seconds(seconds);
        appendInteger(out, 1);
    }
    else if (cmd == "TTL" && args.size() >= 2) {
        bool exists = store.count(args[1]) || listStore.count(args[1]) || hashStore.count(args[1]);
        if (!exists) { appendInteger(out, -2); return; }
        auto it = expiryTimes.find(args[1]);
        if (it == expiryTimes.end()) { appendInteger(out, -1); return; }
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second - Clock::now()).count();
        appendInteger(out, remaining > 0 ? remaining : 0);
    }
    else if (cmd == "PERSIST" && args.size() >= 2) {
        int removed = expiryTimes.erase(args[1]);
        appendInteger(out, removed);
    }
    else if (cmd == "EXISTS" && args.size() >= 2) {
        isExpired(args[1]);
        bool exists = store.count(args[1]) || listStore.count(args[1]) || hashStore.count(args[1]);
        appendInteger(out, exists ? 1 : 0);
    }
    else if (cmd == "LPUSH" && args.size() >= 3) {
        for (size_t i = 2; i < args.size(); i++) listStore[args[1]].push_front(args[i]);
        appendInteger(out, listStore[args[1]].size());
    }
    else if (cmd == "RPUSH" && args.size() >= 3) {
        for (size_t i = 2; i < args.size(); i++) listStore[args[1]].push_back(args[i]);
        appendInteger(out, listStore[args[1]].size());
    }
    else if (cmd == "LLEN" && args.size() >= 2) {
        auto it = listStore.find(args[1]);
        appendInteger(out, it == listStore.end() ? 0 : it->second.size());
    }
    else if (cmd == "LRANGE" && args.size() >= 4) {
        auto it = listStore.find(args[1]);
        if (it == listStore.end()) { appendArrayHeader(out, 0); return; }
        auto& lst = it->second;
        int len = lst.size();
        long long start, stop;
        if (!parseIntStrict(args[2], start) || !parseIntStrict(args[3], stop)) {
            appendError(out, "value is not an integer or out of range");
            return;
        }
        if (start < 0) start = std::max((long long)0, len + start);
        if (stop < 0) stop = len + stop;
        if (stop >= len) stop = len - 1;
        if (start > stop || len == 0) { appendArrayHeader(out, 0); return; }
        appendArrayHeader(out, stop - start + 1);
        for (long long i = start; i <= stop; i++) appendBulkString(out, lst[i]);
    }
    else if (cmd == "HSET" && args.size() >= 4) {
        int added = 0;
        for (size_t i = 2; i + 1 < args.size(); i += 2) {
            if (hashStore[args[1]].find(args[i]) == hashStore[args[1]].end()) added++;
            hashStore[args[1]][args[i]] = args[i + 1];
        }
        appendInteger(out, added);
    }
    else if (cmd == "HGET" && args.size() >= 3) {
        auto it = hashStore.find(args[1]);
        if (it == hashStore.end()) { appendNull(out); return; }
        auto fit = it->second.find(args[2]);
        if (fit == it->second.end()) appendNull(out);
        else appendBulkString(out, fit->second);
    }
    else if (cmd == "HGETALL" && args.size() >= 2) {
        auto it = hashStore.find(args[1]);
        if (it == hashStore.end()) { appendArrayHeader(out, 0); return; }
        appendArrayHeader(out, it->second.size() * 2);
        for (auto& kv : it->second) { appendBulkString(out, kv.first); appendBulkString(out, kv.second); }
    }
    else if (cmd == "HDEL" && args.size() >= 3) {
        auto it = hashStore.find(args[1]);
        if (it == hashStore.end()) { appendInteger(out, 0); return; }
        int deleted = it->second.erase(args[2]);
        appendInteger(out, deleted);
    }
    else {
        appendError(out, "unknown command '" + cmd + "'");
    }
}

void loadAOF() {
    std::ifstream in(AOF_FILE, std::ios::binary);
    if (!in.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    size_t pos = 0;
    while (pos < content.size()) {
        size_t consumed;
        std::vector<std::string> args;
        std::string errMsg;
        std::string remaining = content.substr(pos);
        ParseStatus status = tryParseCommand(remaining, consumed, args, errMsg);
        if (status != ParseStatus::OK) break;
        std::string dummy;
        ClientState dummyClient;
        dummyClient.authenticated = true;
        handleCommand(dummy, args, dummyClient, true);
        pos += consumed;
    }
    std::cout << "Loaded " << pos << " bytes from AOF\n";
}

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--requirepass" && i + 1 < argc) {
            requirePass = argv[i + 1];
        }
    }

    loadAOF();
    aofStream.open(AOF_FILE, std::ios::app | std::ios::binary);

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

    std::cout << "my-redis (epoll) listening on 127.0.0.1:6380..." << (requirePass.empty() ? " (no auth)" : " (auth required)") << "\n";

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