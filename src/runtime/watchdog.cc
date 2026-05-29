#include "runtime/watchdog.h"

namespace b70 {

void Watchdog::note_start(const std::string& collector_name) {
    entries_[collector_name];  // ensure entry exists
}

void Watchdog::note_end(const std::string& collector_name, std::uint64_t elapsed_ns) {
    auto& e = entries_[collector_name];
    e.last_slow = elapsed_ns >= timeout_ns_;
    if (e.last_slow) {
        ++e.consecutive_slow;
        if (e.consecutive_slow >= 3) e.disabled = true;
    } else {
        e.consecutive_slow = 0;
    }
}

bool Watchdog::is_disabled(const std::string& collector_name) const {
    auto it = entries_.find(collector_name);
    return it != entries_.end() && it->second.disabled;
}

bool Watchdog::last_was_slow(const std::string& collector_name) const {
    auto it = entries_.find(collector_name);
    return it != entries_.end() && it->second.last_slow;
}

}
