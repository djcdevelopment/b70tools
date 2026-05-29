#pragma once

#include "collectors/collector.h"
#include "collectors/d3dkmt_min.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

class D3DKMTAdapterPerfdataCollector : public Collector {
public:
    D3DKMTAdapterPerfdataCollector();
    ~D3DKMTAdapterPerfdataCollector() override;

    const char* name() const override { return "d3dkmt_adapter_perfdata"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Bound {
        std::string adapter_id;
        std::uint64_t luid_raw = 0;
        D3DKMT_HANDLE handle = 0;
    };

    HMODULE gdi32_ = nullptr;
    PFN_D3DKMTOpenAdapterFromLuid pOpen_ = nullptr;
    PFN_D3DKMTQueryAdapterInfo    pQuery_ = nullptr;
    PFN_D3DKMTCloseAdapter        pClose_ = nullptr;
    std::vector<Bound> bound_;
    std::string init_error_;
    bool failure_reported_ = false;
};

}
