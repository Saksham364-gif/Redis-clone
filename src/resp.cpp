#include "resp.h"

const int MAX_ARRAY_COUNT = 1024;
const int MAX_BULK_LEN = 512 * 1024 * 1024;
const int MAX_LINE_LEN = 64;

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