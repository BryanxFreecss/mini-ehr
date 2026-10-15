#pragma once
// Emoji support for chat: :shortcode: and text-emoticon expansion, plus UTF-8 helpers.
// Strings are UTF-8 throughout; the console is switched to UTF-8 in util::initConsole().
#include <string>
#include <utility>
#include <vector>

#include "util.hpp"

namespace emoji {

inline const std::vector<std::pair<std::string, std::string>>& table() {
    static const std::vector<std::pair<std::string, std::string>> t = {
        // faces
        {"smile", "😄"}, {"grin", "😁"}, {"joy", "😂"}, {"wink", "😉"}, {"slight_smile", "🙂"},
        {"sweat_smile", "😅"}, {"cry", "😢"}, {"sob", "😭"}, {"thinking", "🤔"}, {"sunglasses", "😎"},
        {"nerd", "🤓"}, {"hug", "🤗"}, {"party", "🥳"}, {"mask", "😷"}, {"sick", "🤒"}, {"sleepy", "😴"},
        {"scream", "😱"}, {"pleading", "🥺"}, {"relieved", "😌"},
        // gestures / reactions
        {"thumbsup", "👍"}, {"+1", "👍"}, {"thumbsdown", "👎"}, {"-1", "👎"}, {"clap", "👏"}, {"pray", "🙏"},
        {"muscle", "💪"}, {"ok", "👌"}, {"wave", "👋"}, {"eyes", "👀"}, {"raised_hands", "🙌"},
        {"heart", "❤️"}, {"broken_heart", "💔"}, {"fire", "🔥"}, {"sparkles", "✨"}, {"tada", "🎉"},
        {"100", "💯"}, {"star", "⭐"}, {"rocket", "🚀"},
        // status
        {"check", "✅"}, {"x", "❌"}, {"warning", "⚠️"}, {"rotating_light", "🚨"}, {"question", "❓"},
        {"bell", "🔔"}, {"lock", "🔒"}, {"clock", "🕐"}, {"calendar", "📅"}, {"memo", "📝"},
        // medical
        {"hospital", "🏥"}, {"pill", "💊"}, {"syringe", "💉"}, {"stethoscope", "🩺"},
        {"thermometer", "🌡️"}, {"ambulance", "🚑"}, {"bandage", "🩹"}, {"dna", "🧬"},
        {"microscope", "🔬"}, {"tooth", "🦷"}, {"brain", "🧠"}, {"bone", "🦴"}, {"blood", "🩸"},
        {"xray", "🩻"}, {"crutch", "🩼"}, {"coffee", "☕"}, {"zzz", "💤"},
    };
    return t;
}

inline const std::vector<std::pair<std::string, std::string>>& emoticons() {
    static const std::vector<std::pair<std::string, std::string>> t = {
        {"<3", "❤️"}, {":)", "🙂"}, {":D", "😄"}, {":(", "🙁"}, {";)", "😉"}, {":P", "😛"}, {":O", "😮"},
    };
    return t;
}

inline const std::string* lookup(const std::string& name) {
    std::string n = util::lower(name);
    for (const auto& [k, v] : table())
        if (k == n) return &v;
    return nullptr;
}

// ---- UTF-8 helpers ----
inline bool isValidUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > n) return false;
        for (int k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

// Number of Unicode code points (assumes valid UTF-8).
inline size_t codepoints(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// Cut to at most `max` code points without splitting a multibyte sequence.
inline std::string truncate(const std::string& s, size_t max) {
    size_t n = 0, i = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == max) break;
            ++n;
        }
        ++i;
    }
    return s.substr(0, i);
}

// Drop control characters (incl. ESC) so chat text can't inject terminal escape codes.
inline std::string stripControl(const std::string& s) {
    std::string o;
    for (unsigned char c : s)
        if (c >= 0x20 && c != 0x7F) o += static_cast<char>(c);
    return o;
}

// ---- Expansion ----
inline bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '+' || c == '-';
}

inline std::string expand(const std::string& in) {
    std::string out;
    size_t i = 0, n = in.size();
    while (i < n) {
        // :shortcode:
        if (in[i] == ':') {
            size_t j = i + 1;
            while (j < n && j - i <= 24 && isNameChar(in[j])) ++j;
            if (j < n && in[j] == ':' && j > i + 1) {
                if (const std::string* e = lookup(in.substr(i + 1, j - i - 1))) {
                    out += *e;
                    i = j + 1;
                    continue;
                }
            }
        }
        // emoticons, only as standalone tokens (so "Note:(see)" and URLs are left alone)
        bool startOk = (i == 0 || in[i - 1] == ' ');
        if (startOk) {
            bool matched = false;
            for (const auto& [k, v] : emoticons()) {
                size_t L = k.size();
                if (in.compare(i, L, k) == 0 && (i + L == n || in[i + L] == ' ')) {
                    out += v;
                    i += L;
                    matched = true;
                    break;
                }
            }
            if (matched) continue;
        }
        out += in[i++];
    }
    return out;
}

// Turn a user argument (":fire:", "fire", or a literal emoji) into an emoji string; "" if invalid.
inline std::string resolve(std::string arg) {
    arg = util::trim(arg);
    if (arg.size() >= 2 && arg.front() == ':' && arg.back() == ':') arg = arg.substr(1, arg.size() - 2);
    if (const std::string* e = lookup(arg)) return *e;
    bool ascii = true;
    for (unsigned char c : arg)
        if (c >= 0x80) ascii = false;
    if (!ascii && isValidUtf8(arg) && arg.size() <= 16 && codepoints(arg) <= 4) return stripControl(arg);
    return "";
}

}  // namespace emoji
