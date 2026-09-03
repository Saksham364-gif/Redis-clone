#pragma once
#include <string>
#include <vector>

struct ClientState {
    std::string inbuf;
    std::string outbuf;
    bool authenticated = false;
};

extern std::string requirePass;

void handleCommand(std::string& out, std::vector<std::string>& args, ClientState& client, bool fromAOF = false);