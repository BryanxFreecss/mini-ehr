#pragma once
// Small helpers: string utils, time, SHA-256 password hashing, console setup.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <cstdint>
#include <ctime>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace util {

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

inline std::string lower(std::string s) {
    for (auto& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline bool containsNoCase(const std::string& hay, const std::string& needle) {
    return lower(hay).find(lower(needle)) != std::string::npos;
}

inline std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == d) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

// Escape so a field can live on one tab-separated line.
inline std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\t': o += "\\t"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            default: o += c;
        }
    }
    return o;
}

inline std::string unescape(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += (n == 't') ? '\t' : (n == 'n') ? '\n' : (n == 'r') ? '\r' : n;
        } else o += s[i];
    }
    return o;
}

inline std::string join(const std::vector<std::string>& v, char d) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) { if (i) o += d; o += v[i]; }
    return o;
}

inline long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline std::string formatTime(std::time_t t, const char* fmt) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

inline std::string nowStamp() { return formatTime(std::time(nullptr), "%Y-%m-%d %H:%M"); }
inline std::string today() { return formatTime(std::time(nullptr), "%Y-%m-%d"); }

inline bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}

// YYYY-MM-DD
inline bool validDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    if (!allDigits(s.substr(0, 4)) || !allDigits(s.substr(5, 2)) || !allDigits(s.substr(8, 2))) return false;
    int m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    return m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// YYYY-MM-DD HH:MM
inline bool validDateTime(const std::string& s) {
    if (s.size() != 16 || s[10] != ' ' || s[13] != ':') return false;
    if (!validDate(s.substr(0, 10))) return false;
    if (!allDigits(s.substr(11, 2)) || !allDigits(s.substr(14, 2))) return false;
    return std::stoi(s.substr(11, 2)) < 24 && std::stoi(s.substr(14, 2)) < 60;
}

// ---- SHA-256 (for salted password hashes; demo-grade, see README) ----
inline std::string sha256hex(const std::string& data) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string msg = data;
    uint64_t bitLen = static_cast<uint64_t>(data.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) msg += '\0';
    for (int i = 7; i >= 0; --i) msg += static_cast<char>((bitLen >> (i * 8)) & 0xff);
    auto rotr = [](uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(uint8_t(msg[off + i * 4])) << 24) | (uint32_t(uint8_t(msg[off + i * 4 + 1])) << 16) |
                   (uint32_t(uint8_t(msg[off + i * 4 + 2])) << 8) | uint32_t(uint8_t(msg[off + i * 4 + 3]));
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (uint32_t v : h)
        for (int i = 7; i >= 0; --i) out += hex[(v >> (i * 4)) & 0xf];
    return out;
}

inline std::string randomSalt() {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 16; ++i) s += hex[rd() % 16];
    return s;
}

inline std::string hashPassword(const std::string& pw, const std::string& salt) {
    std::string h = sha256hex(salt + pw);
    for (int i = 0; i < 5000; ++i) h = sha256hex(h + salt + pw);
    return h;
}

// ---- Console ----
inline void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | 0x0004);  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
#endif
}

// Read a line without echo when stdin is a terminal.
inline bool readSecret(std::string& out) {
#ifdef _WIN32
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool tty = GetConsoleMode(in, &mode) != 0;
    if (tty) SetConsoleMode(in, mode & ~ENABLE_ECHO_INPUT);
    bool ok = static_cast<bool>(std::getline(std::cin, out));
    if (tty) { SetConsoleMode(in, mode); std::cout << "\n"; }
    return ok;
#else
    termios old{};
    bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) { termios t = old; t.c_lflag &= ~ECHO; tcsetattr(STDIN_FILENO, TCSANOW, &t); }
    bool ok = static_cast<bool>(std::getline(std::cin, out));
    if (tty) { tcsetattr(STDIN_FILENO, TCSANOW, &old); std::cout << "\n"; }
    return ok;
#endif
}

}  // namespace util
