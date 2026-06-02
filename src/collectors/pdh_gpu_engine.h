#pragma once

#include "collectors/collector.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

// System-wide per-adapter engine utilization via PDH `\GPU Engine(*)\Running Time`.
// Instance names embed the adapter LUID and engine identity:
//   pid_<pid>_luid_0xHHHHHHHH_0xLLLLLLLL_phys_N_eng_M_engtype_<type>
// We bind by LUID, collapse per-process samples onto unique physical engines,
// compute per-engine delta(runtime)/delta(wall), then sum across 3D / compute /
// copy / video-family engines per adapter.
class PdhGpuEngineCollector : public Collector {
public:
    PdhGpuEngineCollector();
    ~PdhGpuEngineCollector() override;

    const char* name() const override { return "pdh_gpu_engine"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Bound {
        std::string adapter_id;
        std::uint64_t luid_raw = 0;
    };

    struct EngineSample {
        std::uint64_t luid = 0;
        std::uint32_t phys = 0;
        std::uint32_t eng = 0;
        std::uint64_t running_time_100ns = 0;
    };

    void*              hQuery_ = nullptr;      // PDH_HQUERY
    void*              hUtil_  = nullptr;      // PDH_HCOUNTER
    std::vector<Bound> bound_;
    std::vector<EngineSample> prev_samples_;
    std::string        init_error_;
    bool               primed_ = false;
    std::uint64_t      last_collect_qpc_ns_ = 0;
};

}
