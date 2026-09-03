#include "commands.h"
#include "resp.h"
#include "storage.h"
#include "persistence.h"
#include <set>
#include <cctype>

std::string requirePass = "";

const std::set<std::string> WRITE_COMMANDS = {
    "SET", "DEL", "LPUSH", "RPUSH", "HSET", "HDEL", "EXPIRE", "PERSIST"
};

void handleCommand(std::string& out, std::vector<std::string>& args, ClientState& client, bool fromAOF) {
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
        if (!keyExists(args[1])) { appendInteger(out, 0); return; }
        long long seconds;
        if (!parseIntStrict(args[2], seconds)) { appendError(out, "value is not an integer or out of range"); return; }
        expiryTimes[args[1]] = Clock::now() + std::chrono::seconds(seconds);
        appendInteger(out, 1);
    }
    else if (cmd == "TTL" && args.size() >= 2) {
        if (!keyExists(args[1])) { appendInteger(out, -2); return; }
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
        appendInteger(out, keyExists(args[1]) ? 1 : 0);
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