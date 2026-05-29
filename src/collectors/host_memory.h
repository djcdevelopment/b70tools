#pragma once

#include "collectors/collector.h"

#include <cstdint>
#include <string>

namespace b70 {

// System-wide host RAM telemetry via GlobalMemoryStatusEx. Emitted with
// adapter_id "" (no adapter binding) since this is a host-level signal.
// Used by the eval gate as the BIOS-flash safety floor: when Shared GPU Memory
// fallback occurs, host RAM commit climbs into the 20+ GB range and pinning
// the rig that close to OOM has caused the user a non-POST / BIOS reflash
// before. The gate refuses to start any inference workload when host RAM is
// already above the safety threshold.
class HostMemoryCollector : public Collector {
public:
    HostMemoryCollector() = default;
    ~HostMemoryCollector() override = default;

    const char* name() const override { return "host_memory"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    std::string init_error_;
};

}
