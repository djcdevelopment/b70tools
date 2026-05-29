#include "collectors/d3dkmt_query_statistics.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <cstdio>
#include <cstring>

namespace b70 {

namespace {

bool nt_success(LONG s) { return s >= 0; }

const char* segment_group_name(UINT sg) {
    switch (sg) {
        case 0: return "local";       // dedicated VRAM
        case 1: return "non_local";   // shared system memory (Shared GPU Memory)
        default: return "unknown";
    }
}

void emit_u64(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
              std::uint64_t ts, std::string name,
              SemanticDomain d, Unit u, std::uint64_t v) {
    MetricSample m;
    m.metric_name = std::move(name);
    m.adapter_id = adapter_id;
    m.session_epoch = epoch;
    m.semantic_domain = d;
    m.unit = u;
    m.source = Source::D3DKMT_Statistics;
    m.timestamp_qpc = ts;
    m.poll_latency_ns = 0;
    m.sampling_window_ns = 0;
    m.observation_kind = ObservationKind::DirectlyObserved;
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence = Confidence::High;
    m.value = v;
    bus.publish(m);
}

}  // namespace

D3DKMTQueryStatisticsCollector::D3DKMTQueryStatisticsCollector() = default;

D3DKMTQueryStatisticsCollector::~D3DKMTQueryStatisticsCollector() {
    shutdown();
}

CollectorSideEffects D3DKMTQueryStatisticsCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive = true;
    s.intrusiveness = Intrusiveness::TrulyPassive;
    return s;
}

bool D3DKMTQueryStatisticsCollector::init(EventBus& bus,
                                          const std::vector<AdapterIdentity>& adapters) {
    (void)bus;
    gdi32_ = LoadLibraryW(L"gdi32.dll");
    if (!gdi32_) { init_error_ = "could not load gdi32.dll"; return false; }
    pQueryStats_ = reinterpret_cast<PFN_D3DKMTQueryStatistics>(
        GetProcAddress(gdi32_, "D3DKMTQueryStatistics"));
    if (!pQueryStats_) {
        init_error_ = "gdi32 missing D3DKMTQueryStatistics export";
        FreeLibrary(gdi32_);
        gdi32_ = nullptr;
        return false;
    }
    for (const auto& a : adapters) {
        if (a.luid == 0) continue;
        Bound b;
        b.adapter_id = a.adapter_id;
        b.luid.LowPart  = static_cast<DWORD>(a.luid & 0xFFFFFFFFu);
        b.luid.HighPart = static_cast<LONG>(static_cast<std::int32_t>(a.luid >> 32));
        bound_.push_back(b);
    }
    return !bound_.empty();
}

void D3DKMTQueryStatisticsCollector::shutdown() {
    bound_.clear();
    if (gdi32_) { FreeLibrary(gdi32_); gdi32_ = nullptr; }
    pQueryStats_ = nullptr;
}

void D3DKMTQueryStatisticsCollector::poll(std::uint64_t now_qpc_ns,
                                          std::uint32_t session_epoch,
                                          EventBus& bus) {
    if (!pQueryStats_) return;
    for (auto& b : bound_) {
        for (UINT sg = 0; sg <= 1; ++sg) {
            B70_D3DKMT_QUERYSTATISTICS qs;
            std::memset(&qs, 0, sizeof(qs));
            qs.Type                                                = B70_D3DKMT_QS_VIDEO_MEMORY_SEGMENT_GROUP;
            qs.AdapterLuid                                         = b.luid;
            qs.hProcess                                            = nullptr;  // system-wide, not per-process
            qs.QueryResult.VideoMemorySegmentGroup.SegmentGroup    = sg;

            const LONG st = pQueryStats_(&qs);
            if (!nt_success(st)) {
                bool& already = (sg == 0) ? failure_reported_local_ : failure_reported_non_local_;
                if (!already) {
                    DisagreementReport r;
                    r.rule_name = "expected_source_unavailable";
                    r.adapter_id = b.adapter_id;
                    char tmp[200];
                    std::snprintf(tmp, sizeof(tmp),
                                  "D3DKMTQueryStatistics(VIDEO_MEMORY_SEGMENT_GROUP, sg=%u) returned NTSTATUS 0x%08lx",
                                  sg, static_cast<unsigned long>(static_cast<ULONG>(st)));
                    r.explanation = tmp;
                    r.resulting_confidence = Confidence::Low;
                    r.involved_sources.emplace_back("D3DKMT_Statistics");
                    r.session_epoch = session_epoch;
                    r.timestamp_qpc = now_qpc_ns;
                    bus.publish(r);
                    already = true;
                }
                continue;
            }

            const auto& v = qs.QueryResult.VideoMemorySegmentGroup;
            const char* sgn = segment_group_name(v.SegmentGroup);
            char nm[80];

            std::snprintf(nm, sizeof(nm), "gpu.adapter.vram.%s.bytes_committed", sgn);
            emit_u64(bus, b.adapter_id, session_epoch, now_qpc_ns, nm,
                     SemanticDomain::Memory, Unit::Bytes,
                     static_cast<std::uint64_t>(v.BytesCommitted));

            std::snprintf(nm, sizeof(nm), "gpu.adapter.vram.%s.bytes_resident", sgn);
            emit_u64(bus, b.adapter_id, session_epoch, now_qpc_ns, nm,
                     SemanticDomain::Memory, Unit::Bytes,
                     static_cast<std::uint64_t>(v.BytesResident));

            std::snprintf(nm, sizeof(nm), "gpu.adapter.vram.%s.commit_limit", sgn);
            emit_u64(bus, b.adapter_id, session_epoch, now_qpc_ns, nm,
                     SemanticDomain::Memory, Unit::Bytes,
                     static_cast<std::uint64_t>(v.CommitLimit));

            std::snprintf(nm, sizeof(nm), "gpu.adapter.vram.%s.aggregated_allocations", sgn);
            emit_u64(bus, b.adapter_id, session_epoch, now_qpc_ns, nm,
                     SemanticDomain::Memory, Unit::Dimensionless,
                     static_cast<std::uint64_t>(v.AggregatedAllocations));
        }
    }
}

}
