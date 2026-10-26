// Daily keystroke counter (Linux, needs root or "input" group).
// Build: g++ -std=c++17 main.cpp -o keyconter
// Run:   sudo ./keyconter   (writes log.csv, shows live count)
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

static volatile sig_atomic_t running = 1;

static void playAsync(const char *command) {
    std::thread([command]() { std::system(command); }).detach();
}

static std::string today() {
    char buf[16];
    std::time_t t = std::time(nullptr);
    std::strftime(buf, sizeof buf, "%Y-%m-%d", std::localtime(&t));
    return buf;
}

static std::vector<std::string> findKeyboards() {
    std::vector<std::string> res;
    std::ifstream f("/proc/bus/input/devices");
    std::string line, block;
    auto flush = [&]() {
        if (block.find("EV=120013") != std::string::npos) {
            size_t p = block.find("event");
            if (p != std::string::npos) {
                size_t e = p + 5;
                while (e < block.size() && isdigit((unsigned char)block[e])) e++;
                res.push_back("/dev/input/" + block.substr(p, e - p));
            }
        }
        block.clear();
    };
    while (std::getline(f, line)) {
        if (line.empty()) flush();
        else block += line + "\n";
    }
    flush();
    return res;
}

static std::map<std::string, long> loadLog(const std::string &path) {
    std::map<std::string, long> m;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        size_t c = line.find(',');
        if (c == std::string::npos || line.compare(0, 4, "date") == 0) continue;
        try { m[line.substr(0, c)] = std::stol(line.substr(c + 1)); } catch (...) {}
    }
    return m;
}

static void saveLog(const std::string &path, const std::map<std::string, long> &m) {
    std::ofstream f(path, std::ios::trunc);
    f << "date,count\n";
    for (auto &kv : m) f << kv.first << "," << kv.second << "\n";
}

static void draw(const std::string &day, long count, const std::map<std::string, long> &m) {
    std::cout << "\033[2J\033[H";
    std::cout << "=== Key Counter ===\n\n";
    std::cout << "Date  : " << day << "\n";
    std::cout << "Typed : " << count << "\n\n";
    std::cout << "History (log.csv):\n";
    for (auto it = m.rbegin(); it != m.rend(); ++it)
        std::cout << "  " << it->first << "  " << it->second << "\n";
    std::cout << "\nCtrl+C to quit\n" << std::flush;
}

int main() {
    std::signal(SIGINT, [](int) { running = 0; });
    std::signal(SIGTERM, [](int) { running = 0; });

    const std::string logPath = "log.csv";
    std::vector<pollfd> fds;
    for (auto &p : findKeyboards()) {
        int fd = open(p.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd >= 0) fds.push_back({fd, POLLIN, 0});
    }
    if (fds.empty()) {
        std::cerr << "No keyboard device opened. Run with sudo.\n";
        return 1;
    }

    auto log = loadLog(logPath);
    std::string day = today();
    log[day];
    bool dirty = true;
    while (running) {
        int r = poll(fds.data(), fds.size(), 1000);
        if (r > 0) {
            for (auto &p : fds) {
                if (!(p.revents & POLLIN)) continue;
                input_event ev;
                while (read(p.fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
                    if (ev.type == EV_KEY && ev.value == 1) {
                        long dailyCount = ++log[day];
                        dirty = true;
                        // Play the level-up souzand at each 500-key daily milestone.
                        if (dailyCount % 50 == 0) {
                            system("mpv ../media/ffxiv.mp3");
                        }

                        if (dailyCount == 5000) {
                            system("mpv ../media/legenad.mp3");
                        }
                    }
                }
            }
        }
        std::string now = today();
        if (now != day) {
            day = now;
            log[day];
            dirty = true;
        }
        if (dirty) {
            saveLog(logPath, log);
            draw(day, log[day], log);
            dirty = false;
        }
    }
    for (auto &p : fds) close(p.fd);
    saveLog(logPath, log);
    std::cout << "\nSaved.\n";
    return 0;
}
