#pragma once

#include "schema/metric_sample.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace b70 {

class DeltaFilter {
public:
    DeltaFilter();

    void set_enabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    void set_heartbeat_interval_ns(std::uint64_t ns) { heartbeat_ns_ = ns; }

    bool should_emit(const MetricSample& m, std::uint64_t now_qpc_ns);

    void note_heartbeat(std::uint64_t now_qpc_ns) { last_heartbeat_ns_ = now_qpc_ns; }
    bool heartbeat_due(std::uint64_t now_qpc_ns) const {
        return enabled_ && (now_qpc_ns - last_heartbeat_ns_) >= heartbeat_ns_;
    }

    void clear();

private:
    struct LastEmit {
        MetricValue value;
        std::uint32_t flags = 0;
        std::uint8_t confidence = 0;
        bool initialized = false;
    };

    static std::string key_of(const MetricSample& m);

    bool enabled_ = true;
    std::uint64_t heartbeat_ns_ = 30ull * 1'000'000'000ull;
    std::uint64_t last_heartbeat_ns_ = 0;
    std::unordered_map<std::string, LastEmit> last_;
};

}
