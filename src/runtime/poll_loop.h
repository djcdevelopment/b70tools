#pragma once

#include "bus/event_bus.h"
#include "collectors/collector.h"
#include "runtime/session.h"
#include "runtime/watchdog.h"

#include <atomic>
#include <cstdint>
#include <random>
#include <vector>

namespace b70 {

class PollLoop {
public:
    struct Options {
        std::uint64_t cadence_ns = 1'000'000'000ull;
        std::uint64_t jitter_ns  = 50'000'000ull;
        std::uint64_t max_ticks  = 0;  // 0 = unlimited
        bool sleep_between_ticks = true;
    };

    PollLoop(EventBus* bus, Session* session, Watchdog* wd, Options o = {});

    void add_collector(Collector* c) { collectors_.push_back(c); }

    void set_stop_flag(std::atomic<bool>* f) { stop_ = f; }

    void run_until_max_ticks();

    std::uint64_t ticks_run() const { return ticks_run_; }

private:
    EventBus* bus_;
    Session* session_;
    Watchdog* wd_;
    Options opts_;
    std::vector<Collector*> collectors_;
    std::uint64_t ticks_run_ = 0;
    std::mt19937_64 rng_;
    std::atomic<bool>* stop_ = nullptr;
};

}
