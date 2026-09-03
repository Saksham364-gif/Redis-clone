#include "storage.h"

std::unordered_map<std::string, std::string> store;
std::unordered_map<std::string, std::deque<std::string>> listStore;
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hashStore;
std::unordered_map<std::string, std::unordered_set<std::string>> setStore;
std::unordered_map<std::string, Clock::time_point> expiryTimes;

bool isExpired(const std::string& key) {
    auto it = expiryTimes.find(key);
    if (it == expiryTimes.end()) return false;
    if (Clock::now() >= it->second) {
        store.erase(key);
        listStore.erase(key);
        hashStore.erase(key);
        setStore.erase(key);
        expiryTimes.erase(it);
        return true;
    }
    return false;
}

void clearExpiry(const std::string& key) {
    expiryTimes.erase(key);
}

bool keyExists(const std::string& key) {
    return store.count(key) || listStore.count(key) || hashStore.count(key) || setStore.count(key);
}