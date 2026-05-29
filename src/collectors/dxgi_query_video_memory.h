#pragma once

#include "collectors/collector.h"

#include <cstdint>
#include <string>
#include <vector>

struct IDXGIFactory6;
struct IDXGIAdapter3;

namespace b70 {

class DxgiQueryVideoMemoryCollector : public Collector {
public:
    DxgiQueryVideoMemoryCollector();
    ~DxgiQueryVideoMemoryCollector() override;

    const char* name() const override { return "dxgi_query_video_memory"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;
    void shutdown() override;

private:
    struct Bound {
        std::string adapter_id;
        std::uint64_t luid_raw = 0;
        IDXGIAdapter3* adapter3 = nullptr;
    };

    IDXGIFactory6* factory_ = nullptr;
    std::vector<Bound> bound_;
};

}
