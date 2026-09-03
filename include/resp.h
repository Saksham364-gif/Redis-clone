#pragma once
#include <string>
#include <vector>

enum class ParseStatus { OK, INCOMPLETE, ERROR };

bool parseIntStrict(const std::string& s, long long& out);
ParseStatus tryParseCommand(const std::string& buf, size_t& consumed, std::vector<std::string>& args, std::string& errMsg);
std::string encodeCommand(const std::vector<std::string>& args);

void appendSimpleString(std::string& out, const std::string& s);
void appendError(std::string& out, const std::string& s);
void appendBulkString(std::string& out, const std::string& s);
void appendNull(std::string& out);
void appendInteger(std::string& out, long long val);
void appendArrayHeader(std::string& out, int count);