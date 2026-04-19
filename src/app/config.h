#pragma once

#include <map>
#include <optional>
#include <string>

namespace deltafeedback::app {

// Simple key=value config (lines starting with # are comments).
// Mirrors the format used by the existing deltachatbot-cpp project.
class Config {
public:
    static Config load(const std::string& path);
    bool save(const std::string& path) const;

    bool has(const std::string& key) const;
    std::string get(const std::string& key, const std::string& fallback = "") const;
    int get_int(const std::string& key, int fallback) const;
    void set(const std::string& key, const std::string& value);

    // For tests / iteration.
    const std::map<std::string, std::string>& raw() const { return kv_; }

private:
    std::map<std::string, std::string> kv_;
};

}  // namespace deltafeedback::app
