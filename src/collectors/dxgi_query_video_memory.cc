#include "collectors/dxgi_query_video_memory.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <windows.h>
#include <dxgi1_6.h>

#include <cstdio>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace b70 {

namespace {

void emit_segment(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
                  std::uint64_t ts, const DXGI_QUERY_VIDEO_MEMORY_INFO& info,
                  const char* segment_name) {
    auto base = [&](std::string n) {
        MetricSample m;
        m.metric_name = std::move(n);
        m.adapter_id = adapter_id;
        m.session_epoch = epoch;
        m.semantic_domain = SemanticDomain::Memory;
        m.unit = Unit::Bytes;
        m.source = Source::DXGI_VideoMemoryInfo;
        m.source_detail = segment_name;
        m.timestamp_qpc = ts;
        m.poll_latency_ns = 0;
        m.sampling_window_ns = 0;
        m.observation_kind = ObservationKind::DirectlyObserved;
        m.correlation_method = CorrelationMethod::ProcessID_Filter;  // per-process scope
        m.confidence = Confidence::High;
        return m;
    };

    char nm[96];
    std::snprintf(nm, sizeof(nm), "vram.%s.budget_bytes", segment_name);
    {
        auto m = base(nm);
        m.value = static_cast<std::uint64_t>(info.Budget);
        bus.publish(m);
    }
    std::snprintf(nm, sizeof(nm), "vram.%s.current_usage_bytes", segment_name);
    {
        auto m = base(nm);
        m.value = static_cast<std::uint64_t>(info.CurrentUsage);
        bus.publish(m);
    }
    std::snprintf(nm, sizeof(nm), "vram.%s.available_for_reservation_bytes", segment_name);
    {
        auto m = base(nm);
        m.value = static_cast<std::uint64_t>(info.AvailableForReservation);
        bus.publish(m);
    }
    std::snprintf(nm, sizeof(nm), "vram.%s.current_reservation_bytes", segment_name);
    {
        auto m = base(nm);
        m.value = static_cast<std::uint64_t>(info.CurrentReservation);
        bus.publish(m);
    }
}

}

DxgiQueryVideoMemoryCollector::DxgiQueryVideoMemoryCollector() = default;

DxgiQueryVideoMemoryCollector::~DxgiQueryVideoMemoryCollector() {
    shutdown();
}

CollectorSideEffects DxgiQueryVideoMemoryCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive = true;
    s.intrusiveness = Intrusiveness::TrulyPassive;  // empirically verified clean on this rig
    return s;
}

bool DxgiQueryVideoMemoryCollector::init(EventBus& /*bus*/,
                                         const std::vector<AdapterIdentity>& adapters) {
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory6),
                                     reinterpret_cast<void**>(&factory_));
    if (FAILED(hr) || !factory_) return false;

    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* a1 = nullptr;
        HRESULT er = factory_->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_UNSPECIFIED, __uuidof(IDXGIAdapter1),
            reinterpret_cast<void**>(&a1));
        if (er == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(er) || !a1) continue;

        DXGI_ADAPTER_DESC1 d{};
        a1->GetDesc1(&d);
        const std::uint64_t raw =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.AdapterLuid.HighPart)) << 32)
            | static_cast<std::uint64_t>(d.AdapterLuid.LowPart);

        bool matched = false;
        for (const auto& w : adapters) {
            if (w.luid == raw) {
                IDXGIAdapter3* a3 = nullptr;
                if (SUCCEEDED(a1->QueryInterface(__uuidof(IDXGIAdapter3),
                                                  reinterpret_cast<void**>(&a3))) && a3) {
                    bound_.push_back({w.adapter_id, raw, a3});
                    matched = true;
                }
                break;
            }
        }
        (void)matched;
        a1->Release();
    }
    return !bound_.empty();
}

void DxgiQueryVideoMemoryCollector::shutdown() {
    for (auto& b : bound_) {
        if (b.adapter3) { b.adapter3->Release(); b.adapter3 = nullptr; }
    }
    bound_.clear();
    if (factory_) { factory_->Release(); factory_ = nullptr; }
}

void DxgiQueryVideoMemoryCollector::poll(std::uint64_t now_qpc_ns,
                                         std::uint32_t session_epoch,
                                         EventBus& bus) {
    for (auto& b : bound_) {
        if (!b.adapter3) continue;
        DXGI_QUERY_VIDEO_MEMORY_INFO local{};
        DXGI_QUERY_VIDEO_MEMORY_INFO nonlocal{};
        if (SUCCEEDED(b.adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local))) {
            emit_segment(bus, b.adapter_id, session_epoch, now_qpc_ns, local, "local");
        }
        if (SUCCEEDED(b.adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal))) {
            emit_segment(bus, b.adapter_id, session_epoch, now_qpc_ns, nonlocal, "non_local");
        }
    }
}

}
