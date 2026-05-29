#pragma once

#include "collectors/collector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

// System-wide per-adapter VRAM commit via PDH `\GPU Adapter Memory(*)\Dedicated
// Usage` + `\Shared Usage`. PDH instance names embed the LUID
// ("luid_0xHHHH_0xHHHH_phys_N") which we reconcile to AdapterIdentity. Same
// source Task Manager uses, so the values match what the user sees in
// the OS. This is the cross-process VRAM signal that DXGI and Vulkan
// per-process budgets do NOT provide on Intel's driver.
//
// Rationale for the path choice: D3DKMTQueryStatistics is fragile across
// Windows builds (struct layout drift, type-enum revisions, kernel size
// validation). PDH is documented, stable, and used by ProcessHacker /
// SystemInformer for the same data. See research notes attached to task #69.
class PdhGpuMemoryCollector : public Collector {
public:
    PdhGpuMemoryCollector();
    ~PdhGpuMemoryCollector() override;

    const char* name() const override { return "pdh_gpu_memory"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Bound {
        std::string  adapter_id;
        std::uint64_t luid_raw = 0;
    };

    void*               hQuery_       = nullptr;  // PDH_HQUERY
    void*               hDedicated_   = nullptr;  // PDH_HCOUNTER
    void*               hShared_      = nullptr;
    std::vector<Bound>  bound_;
    std::string         init_error_;
    bool                primed_       = false;
};

}
