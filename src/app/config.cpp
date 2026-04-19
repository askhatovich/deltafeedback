#include "app/config.h"

#include <fstream>
#include <stdexcept>

namespace deltafeedback::app {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

Config Config::load(const std::string& path) {
    Config c;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        c.kv_[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
    }
    return c;
}

bool Config::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    for (const auto& [k, v] : kv_) f << k << '=' << v << '\n';
    return f.good();
}

bool Config::has(const std::string& key) const {
    auto it = kv_.find(key);
    return it != kv_.end() && !it->second.empty();
}

std::string Config::get(const std::string& key, const std::string& fallback) const {
    auto it = kv_.find(key);
    return (it != kv_.end() && !it->second.empty()) ? it->second : fallback;
}

int Config::get_int(const std::string& key, int fallback) const {
    auto it = kv_.find(key);
    if (it == kv_.end() || it->second.empty()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

void Config::set(const std::string& key, const std::string& value) {
    kv_[key] = value;
}

}  // namespace deltafeedback::app
