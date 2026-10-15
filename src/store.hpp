#pragma once
// EHR data model + flat-file persistence (tab-separated, one record per line).
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "util.hpp"

namespace fs = std::filesystem;

enum class Role { Doctor, Nurse, FrontDesk, Admin };

inline const char* roleName(Role r) {
    switch (r) {
        case Role::Doctor: return "Doctor";
        case Role::Nurse: return "Nurse";
        case Role::FrontDesk: return "Front Desk";
        case Role::Admin: return "Admin";
    }
    return "?";
}

inline std::optional<Role> parseRole(const std::string& s) {
    std::string l = util::lower(s);
    if (l == "doctor") return Role::Doctor;
    if (l == "nurse") return Role::Nurse;
    if (l == "frontdesk" || l == "front desk") return Role::FrontDesk;
    if (l == "admin") return Role::Admin;
    return std::nullopt;
}

inline bool isClinical(Role r) { return r == Role::Doctor || r == Role::Nurse; }

struct User {
    std::string username, display, salt, hash;
    Role role = Role::Nurse;
};

struct Medication {
    std::string name, dose, freq, prescriber, date;
    bool active = true;
};

struct Note {
    std::string ts, author, text;
};

struct Patient {
    int id = 0;  // MRN
    std::string name, dob, sex, phone;
    std::vector<std::string> allergies;
    std::vector<Medication> meds;
    std::vector<Note> notes;
};

struct Appointment {
    int id = 0;
    int patientId = 0;
    std::string provider;  // username
    std::string when;      // YYYY-MM-DD HH:MM
    std::string reason;
    std::string status = "Scheduled";  // Scheduled | Checked-in | Completed | Cancelled
};

class Store {
public:
    explicit Store(fs::path dir) : dir_(std::move(dir)) {
        fs::create_directories(dir_);
        loadUsers();
        loadPatients();
        loadAppointments();
    }

    const fs::path& dir() const { return dir_; }

    // ---- users ----
    std::vector<User> users;

    User* findUser(const std::string& name) {
        for (auto& u : users)
            if (util::lower(u.username) == util::lower(name)) return &u;
        return nullptr;
    }

    bool addUser(const std::string& username, const std::string& display, Role role, const std::string& password) {
        if (findUser(username)) return false;
        User u;
        u.username = username;
        u.display = display;
        u.role = role;
        u.salt = util::randomSalt();
        u.hash = util::hashPassword(password, u.salt);
        users.push_back(u);
        saveUsers();
        return true;
    }

    bool checkLogin(const std::string& username, const std::string& pw, User*& out) {
        out = findUser(username);
        // Hash even for unknown users so timing doesn't reveal which usernames exist.
        std::string salt = out ? out->salt : "0000000000000000";
        std::string h = util::hashPassword(pw, salt);
        return out && h == out->hash;
    }

    std::string displayName(const std::string& username) {
        User* u = findUser(username);
        return u ? u->display : username;
    }

    // ---- patients ----
    std::vector<Patient> patients;

    Patient* findPatient(int id) {
        for (auto& p : patients)
            if (p.id == id) return &p;
        return nullptr;
    }

    Patient& addPatient(Patient p) {
        int next = 1001;
        for (auto& x : patients) next = std::max(next, x.id + 1);
        p.id = next;
        patients.push_back(std::move(p));
        savePatients();
        return patients.back();
    }

    // ---- appointments ----
    std::vector<Appointment> appointments;

    Appointment& addAppointment(Appointment a) {
        int next = 1;
        for (auto& x : appointments) next = std::max(next, x.id + 1);
        a.id = next;
        appointments.push_back(std::move(a));
        saveAppointments();
        return appointments.back();
    }

    Appointment* findAppointment(int id) {
        for (auto& a : appointments)
            if (a.id == id) return &a;
        return nullptr;
    }

    // Returns the clashing appointment, if the provider already has one at that time.
    Appointment* conflict(const std::string& provider, const std::string& when) {
        for (auto& a : appointments)
            if (a.provider == provider && a.when == when && a.status != "Cancelled") return &a;
        return nullptr;
    }

    // ---- audit ----
    void audit(const std::string& user, const std::string& action) {
        std::ofstream f(dir_ / "audit.log", std::ios::app | std::ios::binary);
        f << util::nowStamp() << '\t' << util::escape(user) << '\t' << util::escape(action) << '\n';
    }

    std::vector<std::string> readAudit() {
        std::vector<std::string> out;
        std::ifstream f(dir_ / "audit.log", std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            auto p = util::split(line, '\t');
            if (p.size() >= 3) out.push_back(p[0] + "  " + util::unescape(p[1]) + "  " + util::unescape(p[2]));
        }
        return out;
    }

    void savePatients() {
        atomicWrite("patients.db", [&](std::ostream& o) {
            for (auto& p : patients) {
                o << "P\t" << p.id << '\t' << util::escape(p.name) << '\t' << util::escape(p.dob) << '\t'
                  << util::escape(p.sex) << '\t' << util::escape(p.phone) << '\n';
                for (auto& a : p.allergies) o << "A\t" << p.id << '\t' << util::escape(a) << '\n';
                for (auto& m : p.meds)
                    o << "M\t" << p.id << '\t' << util::escape(m.name) << '\t' << util::escape(m.dose) << '\t'
                      << util::escape(m.freq) << '\t' << util::escape(m.prescriber) << '\t' << util::escape(m.date)
                      << '\t' << (m.active ? 1 : 0) << '\n';
                for (auto& n : p.notes)
                    o << "N\t" << p.id << '\t' << util::escape(n.ts) << '\t' << util::escape(n.author) << '\t'
                      << util::escape(n.text) << '\n';
            }
        });
    }

    void saveAppointments() {
        atomicWrite("appointments.tsv", [&](std::ostream& o) {
            for (auto& a : appointments)
                o << a.id << '\t' << a.patientId << '\t' << util::escape(a.provider) << '\t' << util::escape(a.when)
                  << '\t' << util::escape(a.reason) << '\t' << util::escape(a.status) << '\n';
        });
    }

    void saveUsers() {
        atomicWrite("users.tsv", [&](std::ostream& o) {
            for (auto& u : users)
                o << util::escape(u.username) << '\t' << util::escape(u.display) << '\t' << roleName(u.role) << '\t'
                  << u.salt << '\t' << u.hash << '\n';
        });
    }

private:
    fs::path dir_;

    template <class F>
    void atomicWrite(const char* name, F&& body) {
        fs::path tmp = dir_ / (std::string(name) + ".tmp");
        {
            std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
            body(o);
        }
        fs::rename(tmp, dir_ / name);  // replaces the target on both Windows and POSIX
    }

    static int toInt(const std::string& s) { return util::allDigits(s) ? std::stoi(s) : 0; }

    void loadUsers() {
        std::ifstream f(dir_ / "users.tsv", std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            auto p = util::split(line, '\t');
            if (p.size() < 5) continue;
            auto role = parseRole(p[2]);
            if (!role) continue;
            users.push_back({util::unescape(p[0]), util::unescape(p[1]), p[3], p[4], *role});
        }
    }

    void loadPatients() {
        std::ifstream f(dir_ / "patients.db", std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            auto p = util::split(line, '\t');
            if (p.empty()) continue;
            if (p[0] == "P" && p.size() >= 6) {
                Patient pt;
                pt.id = toInt(p[1]);
                pt.name = util::unescape(p[2]);
                pt.dob = util::unescape(p[3]);
                pt.sex = util::unescape(p[4]);
                pt.phone = util::unescape(p[5]);
                patients.push_back(pt);
            } else if (p.size() >= 3) {
                Patient* pt = findPatient(toInt(p[1]));
                if (!pt) continue;
                if (p[0] == "A") pt->allergies.push_back(util::unescape(p[2]));
                else if (p[0] == "M" && p.size() >= 8)
                    pt->meds.push_back({util::unescape(p[2]), util::unescape(p[3]), util::unescape(p[4]),
                                        util::unescape(p[5]), util::unescape(p[6]), p[7] == "1"});
                else if (p[0] == "N" && p.size() >= 5)
                    pt->notes.push_back({util::unescape(p[2]), util::unescape(p[3]), util::unescape(p[4])});
            }
        }
    }

    void loadAppointments() {
        std::ifstream f(dir_ / "appointments.tsv", std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            auto p = util::split(line, '\t');
            if (p.size() < 6) continue;
            Appointment a;
            a.id = toInt(p[0]);
            a.patientId = toInt(p[1]);
            a.provider = util::unescape(p[2]);
            a.when = util::unescape(p[3]);
            a.reason = util::unescape(p[4]);
            a.status = util::unescape(p[5]);
            appointments.push_back(a);
        }
    }
};
