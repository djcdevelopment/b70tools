#include "collectors/pdh_gpu_memory.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace b70 {

namespace {

// PDH instance names for GPU Adapter Memory are formatted as:
//   "luid_0xHHHHHHHH_0xHHHHHHHH_phys_N"
// where the FIRST hex group is LUID HighPart, the SECOND is LowPart (verified
// against ProcessHacker source). Returns 0 on parse failure.
std::uint64_t parse_pdh_instance_luid(std::wstring_view name) {
    auto p = name.find(L"luid_0x");
    if (p == std::wstring_view::npos) return 0;
    p += 7;  // skip "luid_0x"
    if (p + 8 > name.size()) return 0;
    wchar_t hi_buf[9];
    std::memcpy(hi_buf, name.data() + p, 8 * sizeof(wchar_t));
    hi_buf[8] = 0;
    p += 8;
    if (p + 3 > name.size())  return 0;
    if (name[p] != L'_' || name[p+1] != L'0' || name[p+2] != L'x') return 0;
    p += 3;
    if (p + 8 > name.size()) return 0;
    wchar_t lo_buf[9];
    std::memcpy(lo_buf, name.data() + p, 8 * sizeof(wchar_t));
    lo_buf[8] = 0;

    wchar_t* end = nullptr;
    const unsigned long hi = std::wcstoul(hi_buf, &end, 16);
    if (!end || *end != 0) return 0;
    const unsigned long lo = std::wcstoul(lo_buf, &end, 16);
    if (!end || *end != 0) return 0;
    return (static_cast<std::uint64_t>(hi) << 32) | static_cast<std::uint64_t>(lo);
}

void emit_bytes(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
                std::uint64_t ts, std::string name, std::uint64_t v) {
    MetricSample m;
    m.metric_name        = std::move(name);
    m.adapter_id         = adapter_id;
    m.session_epoch      = epoch;
    m.semantic_domain    = SemanticDomain::Memory;
    m.unit               = Unit::Bytes;
    m.source             = Source::PDH_AdapterMemory;
    m.timestamp_qpc      = ts;
    m.poll_latency_ns    = 0;
    m.sampling_window_ns = 0;
    m.observation_kind   = ObservationKind::DirectlyObserved;
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence         = Confidence::High;
    m.value              = v;
    bus.publish(m);
}

}  // namespace

PdhGpuMemoryCollector::PdhGpuMemoryCollector() = default;

PdhGpuMemoryCollector::~PdhGpuMemoryCollector() {
    shutdown();
}

CollectorSideEffects PdhGpuMemoryCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive    = true;
    s.intrusiveness  = Intrusiveness::TrulyPassive;
    return s;
}

bool PdhGpuMemoryCollector::init(EventBus& /*bus*/,
                                 const std::vector<AdapterIdentity>& adapters) {
    PDH_HQUERY hq = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &hq) != ERROR_SUCCESS) {
        init_error_ = "PdhOpenQueryW failed";
        return false;
    }
    hQuery_ = hq;

    PDH_HCOUNTER hDed = nullptr;
    PDH_HCOUNTER hShr = nullptr;
    // PdhAddEnglishCounter pins counter names to English so the call works
    // across UI locales. Wildcard "*" matches every adapter instance; we
    // filter to our LUIDs at poll time.
    if (PdhAddEnglishCounterW(hq, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &hDed) != ERROR_SUCCESS) {
        init_error_ = "PdhAddEnglishCounterW(Dedicated Usage) failed";
        PdhCloseQuery(hq); hQuery_ = nullptr;
        return false;
    }
    if (PdhAddEnglishCounterW(hq, L"\\GPU Adapter Memory(*)\\Shared Usage", 0, &hShr) != ERROR_SUCCESS) {
        init_error_ = "PdhAddEnglishCounterW(Shared Usage) failed";
        PdhCloseQuery(hq); hQuery_ = nullptr;
        return false;
    }
    hDedicated_ = hDed;
    hShared_    = hShr;

    for (const auto& a : adapters) {
        if (a.luid == 0) continue;
        Bound b;
        b.adapter_id = a.adapter_id;
        b.luid_raw   = a.luid;
        bound_.push_back(b);
    }

    // Prime: PDH needs at least one prior collect before the first formatted
    // read returns valid data on some counter types. Do it now so the very
    // first poll() returns real values.
    PdhCollectQueryData(hq);
    primed_ = true;
    return !bound_.empty();
}

void PdhGpuMemoryCollector::shutdown() {
    if (hQuery_) {
        PdhCloseQuery(hQuery_);
        hQuery_     = nullptr;
        hDedicated_ = nullptr;
        hShared_    = nullptr;
    }
    bound_.clear();
}

namespace {

// Read a PDH counter as PDH_FMT_LARGE array, then for each (luid → value)
// match against our bound adapters and emit `metric_name`.
void read_and_emit(PDH_HCOUNTER hCounter,
                   EventBus& bus,
                   const std::vector<PdhGpuMemoryCollector* /*placeholder*/>& /*_*/,
                   const std::vector<std::pair<std::uint64_t, std::string>>& luid_to_adapter,
                   std::uint64_t now_qpc_ns,
                   std::uint32_t session_epoch,
                   const char* metric_name) {
    if (!hCounter) return;

    DWORD bufSize = 0;
    DWORD itemCount = 0;
    PDH_STATUS s = PdhGetFormattedCounterArrayW(
        hCounter, PDH_FMT_LARGE, &bufSize, &itemCount, nullptr);
    if (s != PDH_MORE_DATA) return;
    if (bufSize == 0 || itemCount == 0) return;

    std::vector<unsigned char> buf(bufSize);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    s = PdhGetFormattedCounterArrayW(
        hCounter, PDH_FMT_LARGE, &bufSize, &itemCount, items);
    if (s != ERROR_SUCCESS) return;

    // Accumulate per-LUID across multiple phys_N instances (Intel B70 is single-die so this
    // is usually 1, but defensive in case the driver exposes multiple physical sub-adapters).
    std::vector<std::pair<std::uint64_t, std::uint64_t>> accum;  // (luid, bytes)
    for (DWORD i = 0; i < itemCount; ++i) {
        if (!items[i].szName) continue;
        const auto luid = parse_pdh_instance_luid(items[i].szName);
        if (luid == 0) continue;
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
            continue;
        }
        const auto v = static_cast<std::uint64_t>(items[i].FmtValue.largeValue);
        bool found = false;
        for (auto& p : accum) {
            if (p.first == luid) { p.second += v; found = true; break; }
        }
        if (!found) accum.emplace_back(luid, v);
    }

    for (const auto& [luid, v] : accum) {
        for (const auto& [bound_luid, adapter_id] : luid_to_adapter) {
            if (bound_luid != luid) continue;
            emit_bytes(bus, adapter_id, session_epoch, now_qpc_ns, metric_name, v);
            break;
        }
    }
}

}  // namespace

void PdhGpuMemoryCollector::poll(std::uint64_t now_qpc_ns,
                                 std::uint32_t session_epoch,
                                 EventBus& bus) {
    if (!hQuery_) return;
    if (PdhCollectQueryData(hQuery_) != ERROR_SUCCESS) return;

    std::vector<std::pair<std::uint64_t, std::string>> luid_to_adapter;
    luid_to_adapter.reserve(bound_.size());
    for (const auto& b : bound_) luid_to_adapter.emplace_back(b.luid_raw, b.adapter_id);

    read_and_emit(hDedicated_, bus, {}, luid_to_adapter, now_qpc_ns, session_epoch,
                  "gpu.adapter.vram.local.bytes_committed");
    read_and_emit(hShared_,    bus, {}, luid_to_adapter, now_qpc_ns, session_epoch,
                  "gpu.adapter.vram.non_local.bytes_committed");
}

}
