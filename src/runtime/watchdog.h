#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace b70 {

class Watchdog {
public:
    void set_timeout_ns(std::uint64_t ns) { timeout_ns_ = ns; }

    void note_start(const std::string& collector_name);
    void note_end(const std::string& collector_name, std::uint64_t elapsed_ns);

    bool is_disabled(const std::string& collector_name) const;
    bool last_was_slow(const std::string& collector_name) const;

    std::uint64_t timeout_ns() const { return timeout_ns_; }

private:
    struct Entry {
        bool disabled = false;
        bool last_slow = false;
        std::uint32_t consecutive_slow = 0;
    };

    std::uint64_t timeout_ns_ = 200ull * 1'000'000ull;
    std::unordered_map<std::string, Entry> entries_;
};

}
