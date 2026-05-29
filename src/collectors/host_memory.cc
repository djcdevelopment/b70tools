#include "collectors/host_memory.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <windows.h>

namespace b70 {

CollectorSideEffects HostMemoryCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive = true;
    s.intrusiveness = Intrusiveness::TrulyPassive;
    return s;
}

bool HostMemoryCollector::init(EventBus& /*bus*/, const std::vector<AdapterIdentity>& /*adapters*/) {
    MEMORYSTATUSEX probe{};
    probe.dwLength = sizeof(probe);
    if (!GlobalMemoryStatusEx(&probe)) {
        init_error_ = "GlobalMemoryStatusEx failed at init";
        return false;
    }
    return true;
}

void HostMemoryCollector::shutdown() {}

namespace {
void emit_u64(EventBus& bus, std::uint32_t epoch, std::uint64_t ts,
              std::string name, std::uint64_t v) {
    MetricSample m;
    m.metric_name        = std::move(name);
    m.adapter_id         = "";  // host-level: no adapter binding
    m.session_epoch      = epoch;
    m.semantic_domain    = SemanticDomain::Memory;
    m.unit               = Unit::Bytes;
    m.source             = Source::Unknown;  // host kernel; no enum bucket for Win32 GMSE
    m.timestamp_qpc      = ts;
    m.poll_latency_ns    = 0;
    m.sampling_window_ns = 0;
    m.observation_kind   = ObservationKind::DirectlyObserved;
    m.correlation_method = CorrelationMethod::Unbound;
    m.confidence         = Confidence::High;
    m.value              = v;
    bus.publish(m);
}
void emit_pct(EventBus& bus, std::uint32_t epoch, std::uint64_t ts,
              std::string name, double v) {
    MetricSample m;
    m.metric_name        = std::move(name);
    m.adapter_id         = "";
    m.session_epoch      = epoch;
    m.semantic_domain    = SemanticDomain::Memory;
    m.unit               = Unit::Percent;
    m.source             = Source::Unknown;
    m.timestamp_qpc      = ts;
    m.poll_latency_ns    = 0;
    m.sampling_window_ns = 0;
    m.observation_kind   = ObservationKind::Inferred;
    m.correlation_method = CorrelationMethod::Unbound;
    m.confidence         = Confidence::High;
    m.value              = v;
    bus.publish(m);
}
}

void HostMemoryCollector::poll(std::uint64_t now_qpc_ns,
                               std::uint32_t session_epoch,
                               EventBus& bus) {
    MEMORYSTATUSEX s{};
    s.dwLength = sizeof(s);
    if (!GlobalMemoryStatusEx(&s)) return;

    const std::uint64_t total   = static_cast<std::uint64_t>(s.ullTotalPhys);
    const std::uint64_t avail   = static_cast<std::uint64_t>(s.ullAvailPhys);
    const std::uint64_t used    = (total > avail) ? (total - avail) : 0;

    emit_u64(bus, session_epoch, now_qpc_ns, "host.memory.total_bytes",     total);
    emit_u64(bus, session_epoch, now_qpc_ns, "host.memory.available_bytes", avail);
    emit_u64(bus, session_epoch, now_qpc_ns, "host.memory.used_bytes",      used);
    emit_pct(bus, session_epoch, now_qpc_ns, "host.memory.used_pct",
             static_cast<double>(s.dwMemoryLoad));

    // Page-file / commit limits — useful for detecting Shared GPU Memory
    // cascades since they often consume backing pagefile-committable space.
    emit_u64(bus, session_epoch, now_qpc_ns, "host.commit.total_bytes",
             static_cast<std::uint64_t>(s.ullTotalPageFile));
    emit_u64(bus, session_epoch, now_qpc_ns, "host.commit.available_bytes",
             static_cast<std::uint64_t>(s.ullAvailPageFile));
}

}
