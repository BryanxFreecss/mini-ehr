// Mini-EHR: a small Epic-style electronic health record for the terminal, with emoji chat.
// DEMO / EDUCATIONAL SOFTWARE - not HIPAA compliant, do not store real patient data.
#include <iostream>
#include <string>

#include "chat.hpp"
#include "emoji.hpp"
#include "store.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <io.h>
#define IS_TTY() (_isatty(_fileno(stdout)) != 0)
#else
#define IS_TTY() (isatty(STDOUT_FILENO) != 0)
#endif

using std::cout;
using std::string;

struct Eof {};  // thrown when stdin closes

// ---------- terminal helpers ----------
static bool g_color = false;
static string sgr(const char* code) { return g_color ? string("\x1b[") + code + "m" : ""; }
static string bold() { return sgr("1"); }
static string dim() { return sgr("2"); }
static string red() { return sgr("31"); }
static string green() { return sgr("32"); }
static string yellow() { return sgr("33"); }
static string cyan() { return sgr("36"); }
static string reset() { return sgr("0"); }

static string ask(const string& prompt, size_t maxChars = 200) {
    cout << prompt << std::flush;
    string s;
    if (!std::getline(std::cin, s)) throw Eof{};
    return emoji::truncate(util::trim(emoji::stripControl(s)), maxChars);
}

static string askRequired(const string& prompt, size_t maxChars = 200) {
    for (;;) {
        string s = ask(prompt, maxChars);
        if (!s.empty()) return s;
        cout << red() << "  Required." << reset() << "\n";
    }
}

static bool confirm(const string& prompt) {
    string s = util::lower(ask(prompt + " (y/n): "));
    return s == "y" || s == "yes";
}

static void heading(const string& t) { cout << "\n" << bold() << "══ " << t << " ══" << reset() << "\n"; }
static void ok(const string& t) { cout << green() << "✔ " << t << reset() << "\n"; }
static void err(const string& t) { cout << red() << "✖ " << t << reset() << "\n"; }

static int ageOf(const string& dob) {
    if (!util::validDate(dob)) return -1;
    string t = util::today();
    int age = std::stoi(t.substr(0, 4)) - std::stoi(dob.substr(0, 4));
    if (t.substr(5) < dob.substr(5)) --age;
    return age;
}

static string pad(const string& s, size_t w) {
    size_t n = emoji::codepoints(s);
    return n >= w ? emoji::truncate(s, w) : s + string(w - n, ' ');
}

// ---------- the app ----------
class App {
public:
    App(const fs::path& dir) : store_(dir), chat_(dir) {}

    void run(bool demo) {
        if (store_.users.empty()) firstRun(demo);
        for (;;) {
            if (!login()) return;
            mainMenu();
            cout << "Logged out.\n";
            if (ask("Log in as another user? (y/n): ") != "y") return;
        }
    }

private:
    Store store_;
    Chat chat_;
    User me_;

    // ----- setup / login -----
    void firstRun(bool demo) {
        if (demo) {
            // Synthetic demo accounts and patients (clearly fake; password is for local demo use only).
            store_.addUser("dr.smith", "Dr. Smith", Role::Doctor, "demo123");
            store_.addUser("nurse.jones", "Nurse Jones", Role::Nurse, "demo123");
            store_.addUser("desk.lee", "Lee (Front Desk)", Role::FrontDesk, "demo123");
            store_.addUser("admin", "Admin", Role::Admin, "demo123");
            Patient a;
            a.name = "Doe, Jane"; a.dob = "1984-03-12"; a.sex = "F"; a.phone = "555-0101";
            a.allergies = {"Penicillin"};
            a.meds = {{"Lisinopril", "10 mg", "daily", "dr.smith", util::today(), true}};
            store_.addPatient(a);
            Patient b;
            b.name = "Roe, Richard"; b.dob = "1962-11-30"; b.sex = "M"; b.phone = "555-0102";
            b.allergies = {"Latex", "Sulfa"};
            store_.addPatient(b);
            chat_.send("general", "nurse.jones", "Good morning team :wave: coffee in the break room :coffee:");
            chat_.send("general", "dr.smith", "Rounds at 3pm :hospital: <3");
            cout << "Demo data created. Accounts: dr.smith, nurse.jones, desk.lee, admin (password: demo123)\n";
            return;
        }
        heading("First-time setup");
        cout << "No users exist yet. Create the administrator account.\n";
        string user = askRequired("Admin username: ", 32);
        string pw;
        for (;;) {
            cout << "Password (min 8 chars): " << std::flush;
            if (!util::readSecret(pw)) throw Eof{};
            if (pw.size() >= 8) break;
            err("Too short.");
        }
        store_.addUser(user, "Admin", Role::Admin, pw);
        store_.audit(user, "created initial admin account");
        ok("Admin created.");
    }

    bool login() {
        heading("Mini-EHR  🏥  Sign in");
        for (int attempt = 0; attempt < 3; ++attempt) {
            string user = ask("Username: ", 32);
            cout << "Password: " << std::flush;
            string pw;
            if (!util::readSecret(pw)) throw Eof{};
            User* u = nullptr;
            if (store_.checkLogin(user, pw, u)) {
                me_ = *u;
                store_.audit(me_.username, "login");
                ok("Welcome, " + me_.display + " (" + roleName(me_.role) + ")");
                return true;
            }
            store_.audit(user.empty() ? "(blank)" : user, "FAILED login");
            err("Invalid username or password.");
        }
        err("Too many failed attempts.");
        return false;
    }

    // ----- main menu -----
    size_t totalUnread() {
        size_t n = 0;
        for (auto& ch : accessibleChannels()) n += chat_.unread(me_.username, ch);
        return n;
    }

    void mainMenu() {
        for (;;) {
            chat_.reload();
            size_t unread = totalUnread();
            heading("Main menu — " + me_.display + " (" + roleName(me_.role) + ")");
            bool clin = isClinical(me_.role);
            bool canReg = clin || me_.role == Role::FrontDesk;
            if (clin) cout << " 1) Patient lookup\n 2) Open chart by MRN\n";
            if (canReg) cout << " 3) Register patient\n";
            if (clin || me_.role == Role::FrontDesk) cout << " 4) Appointments\n";
            cout << " 5) 💬 Chat" << (unread ? yellow() + "  (" + std::to_string(unread) + " unread)" + reset() : "") << "\n";
            if (me_.role == Role::Admin) cout << " 6) Admin tools\n";
            cout << " 0) Log out\n";
            string c = ask("> ");
            if (c == "0") { store_.audit(me_.username, "logout"); return; }
            else if (c == "1" && clin) lookup();
            else if (c == "2" && clin) openByMrn();
            else if (c == "3" && canReg) registerPatient();
            else if (c == "4" && (clin || me_.role == Role::FrontDesk)) appointmentsMenu();
            else if (c == "5") chatMenu();
            else if (c == "6" && me_.role == Role::Admin) adminMenu();
            else err("Unknown option.");
        }
    }

    // ----- patients -----
    void lookup() {
        string q = askRequired("Search name or MRN: ");
        std::vector<Patient*> hits;
        for (auto& p : store_.patients)
            if (util::containsNoCase(p.name, q) || std::to_string(p.id) == q) hits.push_back(&p);
        store_.audit(me_.username, "searched patients: '" + q + "' (" + std::to_string(hits.size()) + " hits)");
        if (hits.empty()) { err("No matching patients."); return; }
        cout << dim() << "  MRN    Name                      DOB         Age" << reset() << "\n";
        for (auto* p : hits)
            cout << "  " << pad(std::to_string(p->id), 6) << " " << pad(p->name, 25) << " " << p->dob << "  " << ageOf(p->dob) << "\n";
        string pick = ask("Open MRN (blank to cancel): ");
        if (util::allDigits(pick)) openChart(std::stoi(pick));
    }

    void openByMrn() {
        string m = askRequired("MRN: ");
        if (!util::allDigits(m)) { err("MRN must be a number."); return; }
        openChart(std::stoi(m));
    }

    void registerPatient() {
        heading("Register patient");
        Patient p;
        p.name = askRequired("Name (Last, First): ");
        for (;;) {
            p.dob = askRequired("Date of birth (YYYY-MM-DD): ");
            if (util::validDate(p.dob) && ageOf(p.dob) >= 0) break;
            err("Use YYYY-MM-DD, not in the future.");
        }
        p.sex = askRequired("Sex (M/F/X): ", 1);
        p.phone = ask("Phone: ", 30);
        Patient& saved = store_.addPatient(p);
        store_.audit(me_.username, "registered patient MRN " + std::to_string(saved.id));
        ok("Registered " + saved.name + " — MRN " + std::to_string(saved.id));
    }

    void openChart(int id) {
        Patient* p = store_.findPatient(id);
        if (!p) { err("No patient with that MRN."); return; }
        store_.audit(me_.username, "opened chart MRN " + std::to_string(id));
        for (;;) {
            printChart(*p);
            cout << " n) Add note   a) Add allergy   p) Prescribe   d) Discontinue med\n"
                 << " h) Note history   s) Schedule appt   c) Chart chat 💬   b) Back\n";
            string c = util::lower(ask("> "));
            bool doctor = me_.role == Role::Doctor;
            if (c == "b") return;
            else if (c == "n") addNote(*p);
            else if (c == "a") addAllergy(*p);
            else if (c == "p" && doctor) prescribe(*p);
            else if (c == "d" && doctor) discontinue(*p);
            else if (c == "p" || c == "d") err("Only doctors can change medication orders.");
            else if (c == "h") { noteHistory(*p); }
            else if (c == "s") scheduleFor(p->id);
            else if (c == "c") chatScreen(Chat::patientChannel(p->id), "Chart chat — " + p->name);
            else err("Unknown option.");
        }
    }

    void printChart(const Patient& p) {
        heading(p.name + "  (MRN " + std::to_string(p.id) + ")");
        cout << "DOB " << p.dob << " (" << ageOf(p.dob) << "y)   Sex " << p.sex << "   Phone " << p.phone << "\n";
        if (p.allergies.empty()) cout << green() << "Allergies: none recorded" << reset() << "\n";
        else cout << red() << "⚠️  Allergies: " << util::join(p.allergies, ',') << reset() << "\n";
        cout << "💊 Active medications:\n";
        bool any = false;
        for (auto& m : p.meds)
            if (m.active) {
                any = true;
                cout << "   • " << m.name << " " << m.dose << " " << m.freq << dim() << "  (" << store_.displayName(m.prescriber)
                     << ", " << m.date << ")" << reset() << "\n";
            }
        if (!any) cout << "   (none)\n";
        cout << "📝 Recent notes:\n";
        size_t start = p.notes.size() > 3 ? p.notes.size() - 3 : 0;
        if (p.notes.empty()) cout << "   (none)\n";
        for (size_t i = start; i < p.notes.size(); ++i)
            cout << "   [" << p.notes[i].ts << "] " << store_.displayName(p.notes[i].author) << ": " << p.notes[i].text << "\n";
    }

    void noteHistory(const Patient& p) {
        heading("Notes — " + p.name);
        if (p.notes.empty()) cout << "(none)\n";
        for (auto& n : p.notes)
            cout << "[" << n.ts << "] " << store_.displayName(n.author) << ": " << n.text << "\n";
        store_.audit(me_.username, "viewed note history MRN " + std::to_string(p.id));
    }

    void addNote(Patient& p) {
        if (!isClinical(me_.role)) { err("Only clinical staff can write notes."); return; }
        string t = askRequired("Note: ", 2000);
        p.notes.push_back({util::nowStamp(), me_.username, t});
        store_.savePatients();
        store_.audit(me_.username, "added note MRN " + std::to_string(p.id));
        ok("Note saved.");
    }

    void addAllergy(Patient& p) {
        string a = askRequired("Allergen: ", 80);
        for (auto& x : p.allergies)
            if (util::lower(x) == util::lower(a)) { err("Already recorded."); return; }
        p.allergies.push_back(a);
        store_.savePatients();
        store_.audit(me_.username, "added allergy '" + a + "' MRN " + std::to_string(p.id));
        ok("Allergy recorded.");
    }

    void prescribe(Patient& p) {
        Medication m;
        m.name = askRequired("Medication: ", 80);
        m.dose = askRequired("Dose (e.g. 10 mg): ", 40);
        m.freq = askRequired("Frequency (e.g. twice daily): ", 60);
        for (auto& a : p.allergies) {
            if (util::containsNoCase(m.name, a) || util::containsNoCase(a, m.name)) {
                cout << red() << bold() << "🚨 ALLERGY ALERT: patient is allergic to " << a << reset() << "\n";
                if (ask("Type OVERRIDE to prescribe anyway: ") != "OVERRIDE") { err("Order cancelled."); return; }
                store_.audit(me_.username, "OVERRODE allergy alert (" + a + ") for " + m.name + " MRN " + std::to_string(p.id));
            }
        }
        for (auto& x : p.meds)
            if (x.active && util::lower(x.name) == util::lower(m.name)) {
                cout << yellow() << "⚠️  Already on an active order for " << x.name << "." << reset() << "\n";
                if (!confirm("Add duplicate order?")) return;
            }
        m.prescriber = me_.username;
        m.date = util::today();
        p.meds.push_back(m);
        store_.savePatients();
        store_.audit(me_.username, "prescribed " + m.name + " MRN " + std::to_string(p.id));
        ok("Medication ordered.");
    }

    void discontinue(Patient& p) {
        std::vector<Medication*> active;
        for (auto& m : p.meds)
            if (m.active) active.push_back(&m);
        if (active.empty()) { err("No active medications."); return; }
        for (size_t i = 0; i < active.size(); ++i) cout << " " << i + 1 << ") " << active[i]->name << " " << active[i]->dose << "\n";
        string s = ask("Discontinue which #: ");
        if (!util::allDigits(s) || std::stoul(s) < 1 || std::stoul(s) > active.size()) { err("Invalid choice."); return; }
        active[std::stoul(s) - 1]->active = false;
        store_.savePatients();
        store_.audit(me_.username, "discontinued " + active[std::stoul(s) - 1]->name + " MRN " + std::to_string(p.id));
        ok("Discontinued.");
    }

    // ----- appointments -----
    void appointmentsMenu() {
        for (;;) {
            heading("Appointments");
            cout << " 1) Upcoming schedule\n 2) Schedule appointment\n 3) Update status\n 0) Back\n";
            string c = ask("> ");
            if (c == "0") return;
            else if (c == "1") listAppointments();
            else if (c == "2") scheduleFor(0);
            else if (c == "3") updateStatus();
            else err("Unknown option.");
        }
    }

    void listAppointments() {
        string today = util::today();
        std::vector<const Appointment*> rows;
        for (auto& a : store_.appointments)
            if (a.when.substr(0, 10) >= today && a.status != "Cancelled" && a.status != "Completed") rows.push_back(&a);
        std::sort(rows.begin(), rows.end(), [](auto* x, auto* y) { return x->when < y->when; });
        if (rows.empty()) { cout << "No upcoming appointments.\n"; return; }
        cout << dim() << "  ID   When              Patient                   Provider           Status      Reason" << reset() << "\n";
        for (auto* a : rows) {
            Patient* p = store_.findPatient(a->patientId);
            cout << "  " << pad(std::to_string(a->id), 4) << " " << a->when << "  " << pad(p ? p->name : "?", 25) << " "
                 << pad(store_.displayName(a->provider), 18) << " " << pad(a->status, 11) << " " << a->reason << "\n";
        }
    }

    void scheduleFor(int patientId) {
        if (patientId == 0) {
            string m = askRequired("Patient MRN: ");
            if (!util::allDigits(m)) { err("MRN must be a number."); return; }
            patientId = std::stoi(m);
        }
        Patient* p = store_.findPatient(patientId);
        if (!p) { err("No patient with that MRN."); return; }
        std::vector<User*> docs;
        for (auto& u : store_.users)
            if (u.role == Role::Doctor) docs.push_back(&u);
        if (docs.empty()) { err("No providers available."); return; }
        for (size_t i = 0; i < docs.size(); ++i) cout << " " << i + 1 << ") " << docs[i]->display << "\n";
        string s = ask("Provider #: ");
        if (!util::allDigits(s) || std::stoul(s) < 1 || std::stoul(s) > docs.size()) { err("Invalid choice."); return; }
        User* doc = docs[std::stoul(s) - 1];
        string when;
        for (;;) {
            when = askRequired("Date & time (YYYY-MM-DD HH:MM): ");
            if (util::validDateTime(when)) break;
            err("Format: 2026-11-03 14:30");
        }
        if (store_.conflict(doc->username, when)) { err(doc->display + " already has an appointment at that time."); return; }
        Appointment a;
        a.patientId = patientId;
        a.provider = doc->username;
        a.when = when;
        a.reason = askRequired("Reason: ", 120);
        Appointment& saved = store_.addAppointment(a);
        store_.audit(me_.username, "scheduled appt #" + std::to_string(saved.id) + " MRN " + std::to_string(patientId));
        ok("Scheduled appointment #" + std::to_string(saved.id) + " for " + p->name + " at " + when);
    }

    void updateStatus() {
        string s = askRequired("Appointment ID: ");
        Appointment* a = util::allDigits(s) ? store_.findAppointment(std::stoi(s)) : nullptr;
        if (!a) { err("No such appointment."); return; }
        cout << " 1) Checked-in  2) Completed  3) Cancelled\n";
        string c = ask("> ");
        if (c == "1") a->status = "Checked-in";
        else if (c == "2") a->status = "Completed";
        else if (c == "3") a->status = "Cancelled";
        else { err("Invalid choice."); return; }
        store_.saveAppointments();
        store_.audit(me_.username, "appt #" + std::to_string(a->id) + " -> " + a->status);
        ok("Updated.");
    }

    // ----- admin -----
    void adminMenu() {
        for (;;) {
            heading("Admin tools");
            cout << " 1) List users\n 2) Create user\n 3) Audit log (last 40)\n 0) Back\n";
            string c = ask("> ");
            if (c == "0") return;
            else if (c == "1") {
                for (auto& u : store_.users) cout << "  " << pad(u.username, 18) << pad(u.display, 22) << roleName(u.role) << "\n";
            } else if (c == "2") createUser();
            else if (c == "3") {
                auto lines = store_.readAudit();
                size_t start = lines.size() > 40 ? lines.size() - 40 : 0;
                for (size_t i = start; i < lines.size(); ++i) cout << "  " << lines[i] << "\n";
                if (lines.empty()) cout << "  (empty)\n";
            } else err("Unknown option.");
        }
    }

    void createUser() {
        string u = askRequired("Username: ", 32);
        if (u.find(':') != string::npos || u.find(' ') != string::npos) { err("No spaces or colons in usernames."); return; }
        if (store_.findUser(u)) { err("Username taken."); return; }
        string d = askRequired("Display name: ", 40);
        std::optional<Role> role;
        while (!role) {
            role = parseRole(askRequired("Role (doctor/nurse/frontdesk/admin): "));
            if (!role) err("Unknown role.");
        }
        string pw;
        for (;;) {
            cout << "Temporary password (min 8): " << std::flush;
            if (!util::readSecret(pw)) throw Eof{};
            if (pw.size() >= 8) break;
            err("Too short.");
        }
        store_.addUser(u, d, *role, pw);
        store_.audit(me_.username, "created user " + u + " (" + roleName(*role) + ")");
        ok("User created.");
    }

    // ----- chat -----
    std::vector<string> accessibleChannels() {
        std::vector<string> out;
        for (auto& c : Chat::fixedChannels())
            if (Chat::canAccess(me_, c)) out.push_back(c);
        for (auto& c : chat_.channelNames())
            if ((c.rfind("dm:", 0) == 0 || c.rfind("patient-", 0) == 0) && Chat::canAccess(me_, c)) out.push_back(c);
        return out;
    }

    string channelTitle(const string& ch) {
        if (ch.rfind("dm:", 0) == 0) {
            auto p = util::split(ch, ':');
            string other = p[1] == util::lower(me_.username) ? p[2] : p[1];
            return "DM — " + store_.displayName(other);
        }
        if (ch.rfind("patient-", 0) == 0) {
            Patient* pt = util::allDigits(ch.substr(8)) ? store_.findPatient(std::stoi(ch.substr(8))) : nullptr;
            return "Chart — " + (pt ? pt->name : ch);
        }
        return "#" + ch;
    }

    void chatMenu() {
        for (;;) {
            chat_.reload();
            heading("💬 Chat");
            auto chans = accessibleChannels();
            for (size_t i = 0; i < chans.size(); ++i) {
                size_t u = chat_.unread(me_.username, chans[i]);
                cout << " " << i + 1 << ") " << channelTitle(chans[i]);
                if (u) cout << yellow() << "  (" << u << " unread)" << reset();
                cout << "\n";
            }
            cout << " d) New direct message   0) Back\n";
            string c = util::lower(ask("> "));
            if (c == "0") return;
            if (c == "d") {
                string who = askRequired("Message which username: ");
                User* u = store_.findUser(who);
                if (!u || util::lower(u->username) == util::lower(me_.username)) { err("No such user."); continue; }
                chatScreen(Chat::dmChannel(me_.username, u->username), "DM — " + u->display);
            } else if (util::allDigits(c) && std::stoul(c) >= 1 && std::stoul(c) <= chans.size()) {
                chatScreen(chans[std::stoul(c) - 1], channelTitle(chans[std::stoul(c) - 1]));
            } else err("Unknown option.");
        }
    }

    void showEmojiPicker() {
        cout << dim() << "Type :name: anywhere in a message. Also works: <3  :)  :D  :(  ;)" << reset() << "\n";
        int col = 0;
        for (auto& [k, v] : emoji::table()) {
            cout << " " << v << " " << pad(":" + k + ":", 17);
            if (++col % 4 == 0) cout << "\n";
        }
        if (col % 4) cout << "\n";
    }

    void chatScreen(const string& ch, const string& title) {
        if (!Chat::canAccess(me_, ch)) { err("You don't have access to that channel."); return; }
        store_.audit(me_.username, "opened chat " + ch);
        std::vector<const ChatMsg*> shown;
        bool redraw = true;
        for (;;) {
            if (redraw) {
                chat_.reload();
                shown = chat_.channel(ch);
                heading(title);
                if (shown.empty()) cout << dim() << "(no messages yet — say hi 👋)" << reset() << "\n";
                for (size_t i = 0; i < shown.size(); ++i) {
                    const ChatMsg& m = *shown[i];
                    bool mine = m.sender == me_.username;
                    cout << dim() << pad("[" + std::to_string(i + 1) + "]", 5) << m.ts.substr(5) << reset() << " "
                         << (mine ? cyan() : bold()) << store_.displayName(m.sender) << reset() << ": " << m.text << "\n";
                    if (!m.reactions.empty()) {
                        cout << "        ";
                        for (auto& r : m.reactions) cout << r.emoji << " " << r.users.size() << "  ";
                        cout << "\n";
                    }
                }
                chat_.markRead(me_.username, ch);
                cout << dim() << "Enter = refresh · /react N :emoji: · /emojis · /who · /help · /back" << reset() << "\n";
            }
            redraw = true;
            string line = ask("💬 > ", 600);
            if (line.empty()) continue;
            if (line[0] == '/') {
                auto sp = line.find(' ');
                string cmd = util::lower(line.substr(0, sp));
                string arg = sp == string::npos ? "" : util::trim(line.substr(sp + 1));
                if (cmd == "/back" || cmd == "/b" || cmd == "/quit") return;
                else if (cmd == "/emojis") { showEmojiPicker(); redraw = false; }
                else if (cmd == "/who") {
                    for (auto& u : store_.users) cout << "  " << pad(u.username, 18) << u.display << " — " << roleName(u.role) << "\n";
                    redraw = false;
                } else if (cmd == "/help") {
                    cout << "  /react N :emoji:  react to message N (again to remove)\n  /emojis           list emoji shortcodes\n"
                            "  /who              list staff\n  /back             leave channel\n  anything else is sent, with :shortcodes: turned into emoji\n";
                    redraw = false;
                } else if (cmd == "/react") {
                    auto sp2 = arg.find(' ');
                    string n = arg.substr(0, sp2);
                    string e = sp2 == string::npos ? "" : emoji::resolve(arg.substr(sp2 + 1));
                    if (!util::allDigits(n) || std::stoul(n) < 1 || std::stoul(n) > shown.size()) { err("Usage: /react <message #> :emoji:"); redraw = false; }
                    else if (e.empty()) { err("Unknown emoji. Try /emojis"); redraw = false; }
                    else chat_.react(shown[std::stoul(n) - 1]->id, me_.username, e);
                } else { err("Unknown command. Try /help"); redraw = false; }
                continue;
            }
            if (!chat_.send(ch, me_.username, line)) { redraw = false; }
        }
    }
};

int main(int argc, char** argv) {
    util::initConsole();
    g_color = IS_TTY();
    fs::path dir = "data";
    bool demo = false;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        if (a == "--demo") demo = true;
        else if (a == "--data" && i + 1 < argc) dir = argv[++i];
        else if (a == "--help" || a == "-h") {
            cout << "Usage: mini-ehr [--data DIR] [--demo]\n  --demo  seed fake demo users/patients on first run\n";
            return 0;
        } else { std::cerr << "Unknown argument: " << a << "\n"; return 2; }
    }
    cout << bold() << "Mini-EHR" << reset() << " — demo software, do not store real patient data.\n";
    try {
        App app(dir);
        app.run(demo);
    } catch (const Eof&) {
        cout << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
