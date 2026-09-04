#include "pubsub.h"
#include "resp.h"
#include <unordered_map>
#include <unistd.h>

std::unordered_map<std::string, std::set<int>> channelSubscribers;
std::unordered_map<int, std::set<std::string>> clientChannels;

void pubsubSubscribe(int fd, const std::string& channel) {
    channelSubscribers[channel].insert(fd);
    clientChannels[fd].insert(channel);
}

void pubsubUnsubscribe(int fd, const std::string& channel) {
    auto it = channelSubscribers.find(channel);
    if (it != channelSubscribers.end()) {
        it->second.erase(fd);
        if (it->second.empty()) channelSubscribers.erase(it);
    }
    auto cit = clientChannels.find(fd);
    if (cit != clientChannels.end()) {
        cit->second.erase(channel);
        if (cit->second.empty()) clientChannels.erase(cit);
    }
}

void pubsubUnsubscribeAll(int fd) {
    auto cit = clientChannels.find(fd);
    if (cit == clientChannels.end()) return;
    std::set<std::string> channels = cit->second;
    for (auto& ch : channels) {
        pubsubUnsubscribe(fd, ch);
    }
}

int pubsubPublish(const std::string& channel, const std::string& message) {
    auto it = channelSubscribers.find(channel);
    if (it == channelSubscribers.end()) return 0;

    std::string payload;
    appendArrayHeader(payload, 3);
    appendBulkString(payload, "message");
    appendBulkString(payload, channel);
    appendBulkString(payload, message);

    int delivered = 0;
    for (int fd : it->second) {
        ssize_t total = 0;
        ssize_t len = (ssize_t)payload.size();
        while (total < len) {
            ssize_t w = write(fd, payload.data() + total, len - total);
            if (w <= 0) break;
            total += w;
        }
        delivered++;
    }
    return delivered;
}

int pubsubSubscriptionCount(int fd) {
    auto it = clientChannels.find(fd);
    return it == clientChannels.end() ? 0 : (int)it->second.size();
}