#pragma once

#include "collectors/collector.h"
#include "collectors/d3dkmt_min.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

// System-wide per-adapter VRAM commit via D3DKMTQueryStatistics. Unlike DXGI
// QueryVideoMemoryInfo and VK_EXT_memory_budget — both of which are scoped to
// the calling process on Intel's driver on this hardware — this kernel API
// sums commits across ALL processes. This is the only known reliable way on
// this rig to detect Shared-GPU-Memory cross-process fallback per card.
class D3DKMTQueryStatisticsCollector : public Collector {
public:
    D3DKMTQueryStatisticsCollector();
    ~D3DKMTQueryStatisticsCollector() override;

    const char* name() const override { return "d3dkmt_query_statistics"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Bound {
        std::string adapter_id;
        LUID        luid;
    };

    HMODULE                    gdi32_       = nullptr;
    PFN_D3DKMTQueryStatistics  pQueryStats_ = nullptr;
    std::vector<Bound>         bound_;
    std::string                init_error_;
    bool                       failure_reported_local_     = false;
    bool                       failure_reported_non_local_ = false;
};

}
