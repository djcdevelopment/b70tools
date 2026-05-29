#pragma once

#include "collectors/collector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

class VulkanMemoryBudgetCollector : public Collector {
public:
    VulkanMemoryBudgetCollector();
    ~VulkanMemoryBudgetCollector() override;

    const char* name() const override { return "vulkan_memory_budget"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Impl;
    Impl* impl_;
};

}
