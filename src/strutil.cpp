#include "strutil.h"

#include <cctype>
#include <cstdio>

using namespace std;

namespace strutil {

string trim(const string &s) {
    size_t begin = 0, end = s.size();
    while (begin < end && isspace(static_cast<unsigned char>(s[begin])))
        ++begin;
    while (end > begin && isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(begin, end - begin);
}

vector<string> split(const string &s, char delim) {
    vector<string> out;
    size_t start = 0;
    for (;;) {
        size_t pos = s.find(delim, start);
        if (pos == string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
}

string join(const vector<string> &parts, const string &delim) {
    string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += delim;
        out += parts[i];
    }
    return out;
}

bool startsWith(const string &s, const string &prefix) {
    if (s.size() < prefix.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (tolower(static_cast<unsigned char>(s[i])) != prefix[i])
            return false;
    return true;
}

string replaceAll(string s, const string &from, const string &to) {
    if (from.empty())
        return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

string humanSize(uint64_t bytes) {
    static const char *units[] = {"bytes", "KB", "MB", "GB", "TB"};
    if (bytes < 1024)
        return to_string(bytes) + " bytes";
    double v = static_cast<double>(bytes);
    size_t unit = 0;
    while (v >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        v /= 1024.0;
        ++unit;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f %s", v, units[unit]);
    return buf;
}

bool tryParseUInt(const string &s, uint64_t &out) {
    if (s.empty())
        return false;
    uint64_t value = 0;
    for (char c : s) {
        if (c < '0' || c > '9')
            return false;
        uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) // would overflow
            return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

} // namespace strutil
