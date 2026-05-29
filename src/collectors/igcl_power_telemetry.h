#pragma once

#include "collectors/collector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

class IgclPowerTelemetryCollector : public Collector {
public:
    IgclPowerTelemetryCollector();
    ~IgclPowerTelemetryCollector() override;

    const char* name() const override { return "igcl_power_telemetry"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Impl;
    Impl* impl_;
};

}
