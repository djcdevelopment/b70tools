#include "runtime/session.h"

#include "schema/events.h"

#include <chrono>

namespace b70 {

void Session::bump_epoch(const std::string& cause, std::uint64_t now_qpc_ns) {
    SessionEpochBoundary e;
    e.previous_epoch = epoch_;
    ++epoch_;
    e.new_epoch = epoch_;
    e.cause = cause;
    e.timestamp_qpc = now_qpc_ns;
    if (bus_) bus_->publish(e);
}

std::uint64_t Session::now_qpc_ns() {
    using clock = std::chrono::steady_clock;
    auto since_epoch = clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count());
}

}
