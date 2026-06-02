#include "runtime/poll_loop.h"

#include "schema/events.h"

#include <chrono>
#include <thread>

namespace b70 {

PollLoop::PollLoop(EventBus* bus, Session* session, Watchdog* wd, Options o)
    : bus_(bus), session_(session), wd_(wd), opts_(o),
      rng_(static_cast<std::uint64_t>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

void PollLoop::add_collector(Collector* c, std::uint64_t period_ticks) {
    if (!c) return;
    if (period_ticks == 0) period_ticks = 1;
    CollectorSlot slot;
    slot.collector = c;
    slot.period_ticks = period_ticks;
    slot.stats.collector_name = c->name();
    collectors_.push_back(std::move(slot));
}

std::vector<PollLoop::CollectorStats> PollLoop::collector_stats() const {
    std::vector<CollectorStats> out;
    out.reserve(collectors_.size());
    for (const auto& slot : collectors_) out.push_back(slot.stats);
    return out;
}

void PollLoop::run_until_max_ticks() {
    while (opts_.max_ticks == 0 || ticks_run_ < opts_.max_ticks) {
        if (stop_ && stop_->load(std::memory_order_relaxed)) break;

        const std::uint64_t now_ns = Session::now_qpc_ns();
        const std::uint32_t epoch = session_ ? session_->epoch() : 0;

        for (auto& slot : collectors_) {
            auto* c = slot.collector;
            if (!c) continue;
            if (slot.period_ticks > 1 && (ticks_run_ % slot.period_ticks) != 0) continue;
            const std::string nm = c->name();
            if (wd_ && wd_->is_disabled(nm)) continue;

            const std::uint64_t t0 = Session::now_qpc_ns();
            if (wd_) wd_->note_start(nm);
            c->poll(now_ns, epoch, *bus_);
            const std::uint64_t t1 = Session::now_qpc_ns();
            slot.stats.poll_calls += 1;
            slot.stats.total_poll_ns += (t1 - t0);
            slot.stats.max_poll_ns = std::max(slot.stats.max_poll_ns, t1 - t0);
            if (wd_) {
                wd_->note_end(nm, t1 - t0);
                if (wd_->is_disabled(nm)) {
                    CollectorAuditRecord rec;
                    rec.collector_name = nm;
                    rec.notes = "watchdog disabled collector for session (3 consecutive timeouts)";
                    rec.session_epoch = epoch;
                    rec.timestamp_qpc = t1;
                    bus_->publish(rec);
                }
            }
        }

        ++ticks_run_;
        if (opts_.after_tick) opts_.after_tick();
        if (opts_.max_ticks && ticks_run_ >= opts_.max_ticks) break;
        if (stop_ && stop_->load(std::memory_order_relaxed)) break;

        if (opts_.sleep_between_ticks) {
            const std::uint64_t jitter = opts_.jitter_ns
                ? (rng_() % (2 * opts_.jitter_ns + 1)) - opts_.jitter_ns
                : 0;
            const std::int64_t sleep_ns =
                static_cast<std::int64_t>(opts_.cadence_ns) + static_cast<std::int64_t>(jitter);
            if (sleep_ns > 0) {
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
            }
        }
    }
}

}
