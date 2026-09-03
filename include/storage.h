#pragma once
#include <string>
#include <deque>
#include <unordered_map>
#include <chrono>

using Clock = std::chrono::steady_clock;

extern std::unordered_map<std::string, std::string> store;
extern std::unordered_map<std::string, std::deque<std::string>> listStore;
extern std::unordered_map<std::string, std::unordered_map<std::string, std::string>> hashStore;
extern std::unordered_map<std::string, Clock::time_point> expiryTimes;

bool isExpired(const std::string& key);
void clearExpiry(const std::string& key);
bool keyExists(const std::string& key);