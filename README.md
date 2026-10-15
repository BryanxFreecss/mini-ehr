# Mini-EHR 🏥

A small Epic-style electronic health record for the terminal, written in C++17, with a built-in staff chat that supports emojis.

> **Demo / educational software.** It is not HIPAA compliant (no encryption at rest, no real session management, flat-file storage). Never put real patient data in it.

## Features

**Clinical**
- Patient registration with auto-assigned MRNs and search by name or MRN
- Patient chart: demographics, allergies, active medications, clinical notes
- Medication orders with an **allergy alert** (explicit `OVERRIDE` required and audited) and duplicate-order warning
- Appointment scheduling with provider double-booking prevention and status tracking

**Security-minded basics**
- Salted, iterated SHA-256 password hashes; passwords are not echoed
- Role-based access (Doctor, Nurse, Front Desk, Admin). Admins manage users and read the audit log but cannot open charts
- Every login, failed login, chart access, order and chat access is written to an audit log

**Chat 💬**
- Channels (`#general`, `#doctors`, `#nursing`, `#frontdesk`) gated by role, direct messages, and a private chat per patient chart
- `:shortcodes:` become emojis (`:pill:` 💊, `:rotating_light:` 🚨, `:tada:` 🎉 ...), plus `<3`, `:)`, `:D`
- You can also paste or type real emojis. `/emojis` lists every shortcode
- Reactions: `/react 3 :thumbsup:` (run it again to remove)
- Unread counts per channel; press Enter in a channel to refresh. Run two terminals as different users against the same data folder to chat live
- Control characters are stripped from messages so nobody can inject terminal escape codes

## Build & run

Requires a C++17 compiler (g++ / MSYS2 MinGW on Windows).

```sh
sh build.sh                      # or: g++ -std=c++17 -O2 src/main.cpp -o mini-ehr
./mini-ehr --demo                # first run: seeds fake users + patients
./mini-ehr                       # later runs
./mini-ehr --data some/folder    # use a different data directory
```

Demo accounts (created by `--demo`, password `demo123`): `dr.smith`, `nurse.jones`, `desk.lee`, `admin`.
Without `--demo`, the first run asks you to create an admin account.

Use Windows Terminal (or any UTF-8 terminal with an emoji font) for the best emoji rendering.

## Layout

| File | Purpose |
| --- | --- |
| `src/main.cpp` | Menus, login, charts, appointments, chat screen |
| `src/store.hpp` | Data model + flat-file persistence |
| `src/chat.hpp` | Channels, messages, reactions, unread tracking |
| `src/emoji.hpp` | Shortcode table, emoticons, UTF-8 helpers |
| `src/util.hpp` | Strings, time, SHA-256, console setup |

Data is stored in `./data` (git-ignored).
