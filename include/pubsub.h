#pragma once
#include <string>
#include <set>
#include <unordered_map>

extern std::unordered_map<std::string, std::set<int>> channelSubscribers;

void pubsubSubscribe(int fd, const std::string& channel);
void pubsubUnsubscribe(int fd, const std::string& channel);
void pubsubUnsubscribeAll(int fd);
int pubsubPublish(const std::string& channel, const std::string& message);
int pubsubSubscriptionCount(int fd);