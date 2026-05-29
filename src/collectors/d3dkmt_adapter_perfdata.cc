#include "collectors/d3dkmt_adapter_perfdata.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <cstdio>
#include <cstring>

namespace b70 {

namespace {

bool nt_success(LONG s) { return s >= 0; }

void emit_u64(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
              std::uint64_t ts, std::uint64_t latency,
              std::string name, SemanticDomain d, Unit u,
              std::uint64_t v) {
    MetricSample m;
    m.metric_name = std::move(name);
    m.adapter_id = adapter_id;
    m.session_epoch = epoch;
    m.semantic_domain = d;
    m.unit = u;
    m.source = Source::D3DKMT_PerfData;
    m.timestamp_qpc = ts;
    m.poll_latency_ns = latency;
    m.sampling_window_ns = 0;
    m.observation_kind = ObservationKind::DirectlyObserved;
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence = Confidence::High;
    m.value = v;
    bus.publish(m);
}

void emit_f64(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
              std::uint64_t ts, std::uint64_t latency,
              std::string name, SemanticDomain d, Unit u,
              double v) {
    MetricSample m;
    m.metric_name = std::move(name);
    m.adapter_id = adapter_id;
    m.session_epoch = epoch;
    m.semantic_domain = d;
    m.unit = u;
    m.source = Source::D3DKMT_PerfData;
    m.timestamp_qpc = ts;
    m.poll_latency_ns = latency;
    m.sampling_window_ns = 0;
    m.observation_kind = ObservationKind::Inferred;  // derived from per-mille / deci-C
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence = Confidence::High;
    m.value = v;
    bus.publish(m);
}

}

D3DKMTAdapterPerfdataCollector::D3DKMTAdapterPerfdataCollector() = default;

D3DKMTAdapterPerfdataCollector::~D3DKMTAdapterPerfdataCollector() {
    shutdown();
}

CollectorSideEffects D3DKMTAdapterPerfdataCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive = true;
    s.intrusiveness = Intrusiveness::TrulyPassive;  // empirically verified clean on this rig
    return s;
}

bool D3DKMTAdapterPerfdataCollector::init(EventBus& bus,
                                          const std::vector<AdapterIdentity>& adapters) {
    gdi32_ = LoadLibraryW(L"gdi32.dll");
    if (!gdi32_) { init_error_ = "could not load gdi32.dll"; return false; }
    pOpen_  = reinterpret_cast<PFN_D3DKMTOpenAdapterFromLuid>(
                GetProcAddress(gdi32_, "D3DKMTOpenAdapterFromLuid"));
    pQuery_ = reinterpret_cast<PFN_D3DKMTQueryAdapterInfo>(
                GetProcAddress(gdi32_, "D3DKMTQueryAdapterInfo"));
    pClose_ = reinterpret_cast<PFN_D3DKMTCloseAdapter>(
                GetProcAddress(gdi32_, "D3DKMTCloseAdapter"));
    if (!pOpen_ || !pQuery_ || !pClose_) {
        init_error_ = "gdi32 missing D3DKMT* exports";
        FreeLibrary(gdi32_);
        gdi32_ = nullptr;
        return false;
    }

    for (const auto& a : adapters) {
        if (a.luid == 0) continue;
        B70_D3DKMT_OPENADAPTERFROMLUID op{};
        op.AdapterLuid.LowPart  = static_cast<DWORD>(a.luid & 0xFFFFFFFFu);
        op.AdapterLuid.HighPart = static_cast<LONG>(static_cast<std::int32_t>(a.luid >> 32));
        if (nt_success(pOpen_(&op))) {
            bound_.push_back({a.adapter_id, a.luid, op.hAdapter});
        } else {
            CollectorAuditRecord rec;
            rec.collector_name = name();
            rec.notes = "D3DKMTOpenAdapterFromLuid failed for " + a.adapter_id;
            rec.session_epoch = 0;
            bus.publish(rec);
        }
    }
    return !bound_.empty();
}

void D3DKMTAdapterPerfdataCollector::shutdown() {
    if (pClose_) {
        for (auto& b : bound_) {
            if (b.handle) {
                B70_D3DKMT_CLOSEADAPTER c{};
                c.hAdapter = b.handle;
                pClose_(&c);
                b.handle = 0;
            }
        }
    }
    bound_.clear();
    if (gdi32_) {
        FreeLibrary(gdi32_);
        gdi32_ = nullptr;
    }
    pOpen_ = nullptr;
    pQuery_ = nullptr;
    pClose_ = nullptr;
}

void D3DKMTAdapterPerfdataCollector::poll(std::uint64_t now_qpc_ns,
                                          std::uint32_t session_epoch,
                                          EventBus& bus) {
    if (!pQuery_) return;
    for (auto& b : bound_) {
        if (!b.handle) continue;

        // Some Windows builds (esp. recent cumulative updates) ship an extended
        // D3DKMT_ADAPTER_PERFDATA. Try a few candidate sizes; first non-INVALID_PARAMETER wins.
        constexpr std::size_t kMaxBuf = 256;
        alignas(8) unsigned char buf[kMaxBuf] = {};
        static const std::size_t kSizes[] = { sizeof(B70_D3DKMT_ADAPTER_PERFDATA), 80, 96, 128, 160, 192, 256 };

        const std::uint64_t t0 = now_qpc_ns;
        LONG st = 0;
        std::size_t accepted_size = 0;
        for (auto sz : kSizes) {
            std::memset(buf, 0, kMaxBuf);
            B70_D3DKMT_QUERYADAPTERINFO qi{};
            qi.hAdapter = b.handle;
            qi.Type = B70_KMTQAITYPE_ADAPTERPERFDATA;
            qi.pPrivateDriverData = buf;
            qi.PrivateDriverDataSize = static_cast<UINT>(sz);
            st = pQuery_(&qi);
            if (nt_success(st)) {
                accepted_size = sz;
                break;
            }
        }
        const std::uint64_t latency = 0;
        if (!nt_success(st)) {
            MetricSample m;
            m.metric_name = "d3dkmt.query_failed";
            m.adapter_id = b.adapter_id;
            m.session_epoch = session_epoch;
            m.semantic_domain = SemanticDomain::Driver;
            m.unit = Unit::Dimensionless;
            m.source = Source::D3DKMT_PerfData;
            m.timestamp_qpc = t0;
            m.poll_latency_ns = latency;
            m.sampling_window_ns = 0;
            m.observation_kind = ObservationKind::DirectlyObserved;
            m.correlation_method = CorrelationMethod::DriverHandle;
            m.confidence = Confidence::Low;
            m.value = static_cast<std::int64_t>(st);
            m.flags = flags::Stale;
            bus.publish(m);

            if (!failure_reported_) {
                DisagreementReport r;
                r.rule_name = "expected_source_unavailable";
                r.adapter_id = b.adapter_id;
                char tmp[160];
                std::snprintf(tmp, sizeof(tmp),
                              "D3DKMTQueryAdapterInfo(KMTQAITYPE_ADAPTERPERFDATA) returned NTSTATUS 0x%08lx; "
                              "falling back to Memory-domain evidence per fallback hierarchy",
                              static_cast<unsigned long>(static_cast<ULONG>(st)));
                r.explanation = tmp;
                r.resulting_confidence = Confidence::Low;
                r.involved_sources.emplace_back("D3DKMT_PerfData");
                r.session_epoch = session_epoch;
                r.timestamp_qpc = t0;
                bus.publish(r);
                failure_reported_ = true;
            }
            continue;
        }
        // The "size hint" we found is itself useful telemetry; emit it once per tick.
        emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                 "d3dkmt.adapter_perfdata.accepted_size",
                 SemanticDomain::Driver, Unit::Bytes, accepted_size);
        const B70_D3DKMT_ADAPTER_PERFDATA& pd =
            *reinterpret_cast<const B70_D3DKMT_ADAPTER_PERFDATA*>(buf);

        emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                 "gpu.memory_frequency_hz", SemanticDomain::Frequency, Unit::Hertz,
                 pd.MemoryFrequency);
        emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                 "gpu.memory_frequency_max_hz", SemanticDomain::Frequency, Unit::Hertz,
                 pd.MaxMemoryFrequency);
        if (pd.MemoryBandwidth > 0) {
            emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                     "gpu.memory_bandwidth_bps", SemanticDomain::Memory, Unit::BytesPerSecond,
                     pd.MemoryBandwidth);
        }
        if (pd.PCIEBandwidth > 0) {
            emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                     "gpu.pcie_bandwidth_bps", SemanticDomain::PCIe, Unit::BytesPerSecond,
                     pd.PCIEBandwidth);
        }
        emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                 "gpu.fan_rpm", SemanticDomain::Thermal, Unit::Rpm,
                 static_cast<std::uint64_t>(pd.FanRPM));
        emit_f64(bus, b.adapter_id, session_epoch, t0, latency,
                 "gpu.power_pct", SemanticDomain::Power, Unit::Percent,
                 static_cast<double>(pd.Power) / 10.0);
        emit_f64(bus, b.adapter_id, session_epoch, t0, latency,
                 "gpu.temperature_c", SemanticDomain::Thermal, Unit::Celsius,
                 static_cast<double>(pd.Temperature) / 10.0);
        emit_u64(bus, b.adapter_id, session_epoch, t0, latency,
                 "gpu.power_state_override", SemanticDomain::Driver, Unit::Dimensionless,
                 static_cast<std::uint64_t>(pd.PowerStateOverride));
    }
}

}
