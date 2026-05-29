#pragma once

#include "bus/event_bus.h"

#include <cstdint>
#include <string>

namespace b70 {

class Session {
public:
    explicit Session(EventBus* bus) : bus_(bus) {}

    std::uint32_t epoch() const { return epoch_; }
    void bump_epoch(const std::string& cause, std::uint64_t now_qpc_ns);

    static std::uint64_t now_qpc_ns();

private:
    EventBus* bus_;
    std::uint32_t epoch_ = 0;
};

}
