#pragma once
// Secure-messaging style chat: channels, DMs, per-patient chart channels, emoji text + reactions.
//
// chat.log is append-only so several terminals (different users) can chat live against the
// same data directory; press Enter in a channel to refresh.
//   M <id> <channel> <sender> <timestamp> <text>
//   R <id> <user> <emoji>            (toggles that user's reaction on message <id>)
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "emoji.hpp"
#include "store.hpp"

struct Reaction {
    std::string emoji;
    std::vector<std::string> users;
};

struct ChatMsg {
    long long id = 0;
    std::string channel, sender, ts, text;
    std::vector<Reaction> reactions;
};

class Chat {
public:
    static constexpr size_t kMaxChars = 500;

    explicit Chat(fs::path dir) : dir_(std::move(dir)) { reload(); }

    static std::vector<std::string> fixedChannels() { return {"general", "doctors", "nursing", "frontdesk"}; }

    static std::string dmChannel(const std::string& a, const std::string& b) {
        std::string x = util::lower(a), y = util::lower(b);
        if (y < x) std::swap(x, y);
        return "dm:" + x + ":" + y;
    }

    static std::string patientChannel(int id) { return "patient-" + std::to_string(id); }

    static bool canAccess(const User& u, const std::string& ch) {
        if (ch == "general") return true;
        if (ch == "doctors") return u.role == Role::Doctor || u.role == Role::Admin;
        if (ch == "nursing") return isClinical(u.role) || u.role == Role::Admin;
        if (ch == "frontdesk") return u.role == Role::FrontDesk || u.role == Role::Admin;
        if (ch.rfind("patient-", 0) == 0) return isClinical(u.role);
        if (ch.rfind("dm:", 0) == 0) {
            auto p = util::split(ch, ':');
            return p.size() == 3 && (p[1] == util::lower(u.username) || p[2] == util::lower(u.username));
        }
        return false;
    }

    void reload() {
        msgs_.clear();
        std::ifstream f(dir_ / "chat.log", std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            auto p = util::split(line, '\t');
            if (p.size() >= 6 && p[0] == "M") {
                ChatMsg m;
                m.id = std::stoll(p[1]);
                m.channel = util::unescape(p[2]);
                m.sender = util::unescape(p[3]);
                m.ts = util::unescape(p[4]);
                m.text = util::unescape(p[5]);
                msgs_.push_back(std::move(m));
            } else if (p.size() >= 4 && p[0] == "R") {
                applyReaction(std::stoll(p[1]), util::unescape(p[2]), util::unescape(p[3]));
            }
        }
        reads_.clear();
        std::ifstream r(dir_ / "reads.tsv", std::ios::binary);
        while (std::getline(r, line)) {
            auto p = util::split(line, '\t');
            if (p.size() >= 3) reads_[util::unescape(p[0]) + "\t" + util::unescape(p[1])] = std::stoll(p[2]);
        }
    }

    // Messages in a channel, oldest first (last `limit` only).
    std::vector<const ChatMsg*> channel(const std::string& ch, size_t limit = 30) const {
        std::vector<const ChatMsg*> out;
        for (auto& m : msgs_)
            if (m.channel == ch) out.push_back(&m);
        if (out.size() > limit) out.erase(out.begin(), out.end() - static_cast<long>(limit));
        return out;
    }

    // Every channel that has at least one message (used to discover DMs and chart channels).
    std::vector<std::string> channelNames() const {
        std::set<std::string> s;
        for (auto& m : msgs_) s.insert(m.channel);
        return {s.begin(), s.end()};
    }

    size_t unread(const std::string& user, const std::string& ch) const {
        long long last = lastRead(user, ch);
        size_t n = 0;
        for (auto& m : msgs_)
            if (m.channel == ch && m.id > last && m.sender != user) ++n;
        return n;
    }

    void markRead(const std::string& user, const std::string& ch) {
        long long newest = 0;
        for (auto& m : msgs_)
            if (m.channel == ch) newest = std::max(newest, m.id);
        reads_[user + "\t" + ch] = newest;
        std::ofstream o(dir_ / "reads.tsv", std::ios::binary | std::ios::trunc);
        for (auto& [k, v] : reads_) {
            auto p = util::split(k, '\t');
            o << util::escape(p[0]) << '\t' << util::escape(p[1]) << '\t' << v << '\n';
        }
    }

    // Expands emoji shortcodes, strips control chars, caps length. Returns false if nothing to send.
    bool send(const std::string& ch, const std::string& sender, const std::string& raw) {
        std::string text = emoji::stripControl(raw);
        text = emoji::truncate(emoji::expand(util::trim(text)), kMaxChars);
        if (text.empty()) return false;
        ChatMsg m;
        m.id = std::max(util::nowMs(), lastId() + 1);
        m.channel = ch;
        m.sender = sender;
        m.ts = util::nowStamp();
        m.text = text;
        std::ofstream o(dir_ / "chat.log", std::ios::app | std::ios::binary);
        o << "M\t" << m.id << '\t' << util::escape(ch) << '\t' << util::escape(sender) << '\t' << util::escape(m.ts)
          << '\t' << util::escape(text) << '\n';
        msgs_.push_back(std::move(m));
        return true;
    }

    // Toggle `user`'s reaction on a message. Returns false if the message doesn't exist.
    bool react(long long msgId, const std::string& user, const std::string& em) {
        if (!applyReaction(msgId, user, em)) return false;
        std::ofstream o(dir_ / "chat.log", std::ios::app | std::ios::binary);
        o << "R\t" << msgId << '\t' << util::escape(user) << '\t' << util::escape(em) << '\n';
        return true;
    }

private:
    fs::path dir_;
    std::vector<ChatMsg> msgs_;
    std::map<std::string, long long> reads_;

    long long lastId() const { return msgs_.empty() ? 0 : msgs_.back().id; }

    long long lastRead(const std::string& user, const std::string& ch) const {
        auto it = reads_.find(user + "\t" + ch);
        return it == reads_.end() ? 0 : it->second;
    }

    bool applyReaction(long long id, const std::string& user, const std::string& em) {
        for (auto& m : msgs_) {
            if (m.id != id) continue;
            for (auto it = m.reactions.begin(); it != m.reactions.end(); ++it) {
                if (it->emoji != em) continue;
                auto u = std::find(it->users.begin(), it->users.end(), user);
                if (u != it->users.end()) {
                    it->users.erase(u);
                    if (it->users.empty()) m.reactions.erase(it);
                } else it->users.push_back(user);
                return true;
            }
            m.reactions.push_back({em, {user}});
            return true;
        }
        return false;
    }
};
