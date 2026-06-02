#include "collectors/pdh_gpu_engine.h"

#include "runtime/session.h"
#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace b70 {

namespace {

std::uint64_t parse_pdh_instance_luid(std::wstring_view name) {
    auto p = name.find(L"luid_0x");
    if (p == std::wstring_view::npos) return 0;
    p += 7;
    if (p + 8 > name.size()) return 0;
    wchar_t hi_buf[9];
    std::memcpy(hi_buf, name.data() + p, 8 * sizeof(wchar_t));
    hi_buf[8] = 0;
    p += 8;
    if (p + 3 > name.size()) return 0;
    if (name[p] != L'_' || name[p + 1] != L'0' || name[p + 2] != L'x') return 0;
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

bool parse_decimal_field(std::wstring_view name, std::wstring_view key, std::uint32_t& out) {
    const auto p = name.find(key);
    if (p == std::wstring_view::npos) return false;
    std::size_t i = p + key.size();
    if (i >= name.size() || name[i] < L'0' || name[i] > L'9') return false;
    std::uint64_t v = 0;
    while (i < name.size() && name[i] >= L'0' && name[i] <= L'9') {
        v = (v * 10u) + static_cast<std::uint64_t>(name[i] - L'0');
        if (v > std::numeric_limits<std::uint32_t>::max()) return false;
        ++i;
    }
    out = static_cast<std::uint32_t>(v);
    return true;
}

std::string narrow_lower_ascii(std::wstring_view ws) {
    std::string out;
    out.reserve(ws.size());
    for (wchar_t wc : ws) {
        const unsigned char c =
            (wc >= 0 && wc <= 0x7f) ? static_cast<unsigned char>(wc) : static_cast<unsigned char>('?');
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string parse_engine_type_family(std::wstring_view name) {
    const auto p = name.find(L"_engtype_");
    if (p == std::wstring_view::npos) return {};
    const auto start = p + 9;
    if (start >= name.size()) return {};
    const auto raw = narrow_lower_ascii(name.substr(start));
    if (raw.empty()) return {};
    if (raw == "3d" || raw == "compute" || raw == "copy" || raw == "video") return raw;
    if (raw.rfind("video", 0) == 0) return "video";
    return {};
}

struct EngineAccumulator {
    std::uint64_t luid = 0;
    std::uint32_t phys = 0;
    std::uint32_t eng = 0;
    std::uint64_t running_time_100ns = 0;
};

void accumulate_engine(std::vector<EngineAccumulator>& engines,
                       std::uint64_t luid,
                       std::uint32_t phys,
                       std::uint32_t eng,
                       std::uint64_t running_time_100ns) {
    for (auto& entry : engines) {
        if (entry.luid == luid && entry.phys == phys && entry.eng == eng) {
            entry.running_time_100ns += running_time_100ns;
            return;
        }
    }
    engines.push_back({luid, phys, eng, running_time_100ns});
}

void emit_pct(EventBus& bus,
              const std::string& adapter_id,
              std::uint32_t epoch,
              std::uint64_t ts,
              std::uint64_t latency,
              std::uint64_t window_ns,
              double pct) {
    MetricSample m;
    m.metric_name        = "gpu.engine.utilization_pct";
    m.adapter_id         = adapter_id;
    m.session_epoch      = epoch;
    m.semantic_domain    = SemanticDomain::EngineActivity;
    m.unit               = Unit::Percent;
    m.source             = Source::PDH_GpuEngine;
    m.timestamp_qpc      = ts;
    m.poll_latency_ns    = latency;
    m.sampling_window_ns = window_ns;
    m.observation_kind   = ObservationKind::DerivedFromDelta;
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence         = Confidence::High;
    m.value              = pct;
    bus.publish(m);
}

}  // namespace

PdhGpuEngineCollector::PdhGpuEngineCollector() = default;

PdhGpuEngineCollector::~PdhGpuEngineCollector() {
    shutdown();
}

CollectorSideEffects PdhGpuEngineCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive   = true;
    s.intrusiveness = Intrusiveness::TrulyPassive;
    return s;
}

bool PdhGpuEngineCollector::init(EventBus& /*bus*/,
                                 const std::vector<AdapterIdentity>& adapters) {
    PDH_HQUERY hq = nullptr;
    if (PdhOpenQueryW(nullptr, 0, &hq) != ERROR_SUCCESS) {
        init_error_ = "PdhOpenQueryW failed";
        return false;
    }
    hQuery_ = hq;

    PDH_HCOUNTER hUtil = nullptr;
    if (PdhAddEnglishCounterW(hq, L"\\GPU Engine(*)\\Running Time", 0, &hUtil) != ERROR_SUCCESS) {
        init_error_ = "PdhAddEnglishCounterW(Running Time) failed";
        PdhCloseQuery(hq);
        hQuery_ = nullptr;
        return false;
    }
    hUtil_ = hUtil;

    for (const auto& a : adapters) {
        if (a.luid == 0) continue;
        bound_.push_back({a.adapter_id, a.luid});
    }

    PdhCollectQueryData(hq);
    primed_ = true;
    last_collect_qpc_ns_ = 0;
    return !bound_.empty();
}

void PdhGpuEngineCollector::shutdown() {
    if (hQuery_) {
        PdhCloseQuery(static_cast<PDH_HQUERY>(hQuery_));
        hQuery_ = nullptr;
        hUtil_ = nullptr;
    }
    bound_.clear();
    prev_samples_.clear();
    primed_ = false;
    last_collect_qpc_ns_ = 0;
}

void PdhGpuEngineCollector::poll(std::uint64_t now_qpc_ns,
                                 std::uint32_t session_epoch,
                                 EventBus& bus) {
    if (!hQuery_ || !hUtil_ || !primed_) return;

    const std::uint64_t t0 = Session::now_qpc_ns();
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(hQuery_)) != ERROR_SUCCESS) return;

    DWORD buf_size = 0;
    DWORD item_count = 0;
    auto* counter = static_cast<PDH_HCOUNTER>(hUtil_);
    PDH_STATUS st = PdhGetFormattedCounterArrayW(
        counter, PDH_FMT_LARGE, &buf_size, &item_count, nullptr);
    if (st != PDH_MORE_DATA || buf_size == 0 || item_count == 0) {
        last_collect_qpc_ns_ = now_qpc_ns;
        return;
    }

    std::vector<unsigned char> buf(buf_size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &buf_size, &item_count, items);
    if (st != ERROR_SUCCESS) {
        last_collect_qpc_ns_ = now_qpc_ns;
        return;
    }

    std::vector<EngineAccumulator> engines;
    engines.reserve(item_count);
    for (DWORD i = 0; i < item_count; ++i) {
        if (!items[i].szName) continue;
        if (items[i].FmtValue.CStatus != ERROR_SUCCESS &&
            items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) {
            continue;
        }
        const std::wstring_view name(items[i].szName);
        const auto family = parse_engine_type_family(name);
        if (family.empty()) continue;

        const auto luid = parse_pdh_instance_luid(name);
        if (luid == 0) continue;

        std::uint32_t phys = 0;
        std::uint32_t eng = 0;
        if (!parse_decimal_field(name, L"_phys_", phys)) continue;
        if (!parse_decimal_field(name, L"_eng_", eng)) continue;

        const auto runtime_100ns = static_cast<std::uint64_t>(
            std::max<LONGLONG>(0, items[i].FmtValue.largeValue));
        accumulate_engine(engines, luid, phys, eng, runtime_100ns);
    }

    struct AdapterPct {
        std::string adapter_id;
        double pct = 0.0;
    };
    std::vector<AdapterPct> per_adapter;
    per_adapter.reserve(bound_.size());
    for (const auto& b : bound_) per_adapter.push_back({b.adapter_id, 0.0});

    const std::uint64_t window_ns =
        (last_collect_qpc_ns_ > 0 && now_qpc_ns > last_collect_qpc_ns_)
            ? (now_qpc_ns - last_collect_qpc_ns_)
            : 0;
    const double window_100ns = static_cast<double>(window_ns) / 100.0;

    for (const auto& engine : engines) {
        double engine_pct = 0.0;
        if (window_100ns > 0.0) {
            std::uint64_t prev_runtime = 0;
            for (const auto& sample : prev_samples_) {
                if (sample.luid == engine.luid &&
                    sample.phys == engine.phys &&
                    sample.eng == engine.eng) {
                    prev_runtime = sample.running_time_100ns;
                    break;
                }
            }
            const auto delta_runtime =
                (engine.running_time_100ns >= prev_runtime)
                    ? (engine.running_time_100ns - prev_runtime)
                    : 0ull;
            engine_pct = std::clamp(
                (static_cast<double>(delta_runtime) / window_100ns) * 100.0,
                0.0,
                100.0);
        }
        for (std::size_t i = 0; i < bound_.size(); ++i) {
            if (bound_[i].luid_raw != engine.luid) continue;
            per_adapter[i].pct += engine_pct;
            break;
        }
    }

    const std::uint64_t t1 = Session::now_qpc_ns();
    const std::uint64_t latency_ns = (t1 > t0) ? (t1 - t0) : 0;
    prev_samples_.clear();
    prev_samples_.reserve(engines.size());
    for (const auto& engine : engines) {
        prev_samples_.push_back({engine.luid, engine.phys, engine.eng, engine.running_time_100ns});
    }
    last_collect_qpc_ns_ = now_qpc_ns;

    for (const auto& entry : per_adapter) {
        emit_pct(bus,
                 entry.adapter_id,
                 session_epoch,
                 now_qpc_ns,
                 latency_ns,
                 window_ns,
                 std::clamp(entry.pct, 0.0, 100.0));
    }
}

}
