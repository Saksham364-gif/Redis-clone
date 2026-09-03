#include "persistence.h"
#include "resp.h"
#include "commands.h"
#include <fstream>
#include <iostream>

const std::string AOF_FILE = "appendonly.aof";
std::ofstream aofStream;

void writeToAOF(std::vector<std::string>& args) {
    if (!aofStream.is_open()) return;
    aofStream << encodeCommand(args);
    aofStream.flush();
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

void openAOF() {
    aofStream.open(AOF_FILE, std::ios::app | std::ios::binary);
}