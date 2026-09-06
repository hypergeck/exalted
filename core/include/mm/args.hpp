#pragma once
// Minimal command-line parsing: --key value, --flag, repeated keys allowed,
// positional arguments collected in order.
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm {

class Args {
public:
    Args(int argc, char** argv, int first = 1) {
        for (int i = first; i < argc; ++i) {
            std::string_view a = argv[i];
            if (a.size() > 2 && a.substr(0, 2) == "--") {
                std::string key(a.substr(2));
                const auto eq = key.find('=');
                if (eq != std::string::npos) {
                    kv_.push_back({key.substr(0, eq), key.substr(eq + 1)});
                } else if (i + 1 < argc && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
                    kv_.push_back({key, argv[++i]});
                } else {
                    kv_.push_back({key, ""});
                }
            } else {
                positional_.push_back(std::string(a));
            }
        }
    }

    bool has(std::string_view key) const {
        for (const auto& p : kv_) if (p.first == key) return true;
        return false;
    }

    std::optional<std::string> get(std::string_view key) const {
        for (const auto& p : kv_) if (p.first == key) return p.second;
        return std::nullopt;
    }

    std::string get_or(std::string_view key, std::string def) const {
        auto v = get(key);
        return v ? *v : def;
    }

    std::vector<std::string> all(std::string_view key) const {
        std::vector<std::string> out;
        for (const auto& p : kv_) if (p.first == key) out.push_back(p.second);
        return out;
    }

    long get_int(std::string_view key, long def) const {
        auto v = get(key);
        if (!v || v->empty()) return def;
        return std::strtol(v->c_str(), nullptr, 0);
    }

    const std::vector<std::string>& positional() const { return positional_; }

private:
    std::vector<std::pair<std::string, std::string>> kv_;
    std::vector<std::string> positional_;
};

// Splits "a,b,c" into ["a","b","c"], trimming spaces, skipping empties.
inline std::vector<std::string> split_list(std::string_view s, char sep = ',') {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else if (c != ' ') cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

}  // namespace mm
