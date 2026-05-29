#include "collectors/fake_collector.h"

#include "schema/events.h"
#include "schema/metric_sample.h"

#include <cmath>
#include <cstdio>

namespace b70 {

FakeCollector::FakeCollector(Options o) : opts_(o) {
    last_state_per_adapter_.assign(opts_.adapters, AdapterState::Unknown);
}

CollectorSideEffects FakeCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive = true;
    s.intrusiveness = Intrusiveness::TrulyPassive;
    return s;
}

bool FakeCollector::init(EventBus& bus, const std::vector<AdapterIdentity>& /*adapters*/) {
    for (std::uint32_t i = 0; i < opts_.adapters; ++i) {
        AdapterIdentity a;
        char id[32];
        std::snprintf(id, sizeof(id), "adapter_%u", i);
        a.adapter_id = id;
        a.luid = 0x1234'0000ull + i;
        a.description = std::string("Synthetic Arc Pro B70 #") + std::to_string(i);
        a.dedicated_video_memory = 32ull * 1024ull * 1024ull * 1024ull;
        a.shared_system_memory   = 16ull * 1024ull * 1024ull * 1024ull;
        a.pci_bdf = (i == 0) ? "0000:01:00.0" : "0000:09:00.0";
        a.driver_uuid = "synthetic-driver-uuid-v0";
        a.session_epoch = 0;
        a.timestamp_qpc = 0;
        a.bindings.push_back("DXGI->LUID:" + std::to_string(a.luid));
        a.bindings.push_back("SetupAPI->BDF:" + a.pci_bdf);
        bus.publish(a);
    }
    return true;
}

void FakeCollector::emit_one_adapter(std::uint32_t idx,
                                     std::uint64_t now_qpc_ns,
                                     std::uint32_t session_epoch,
                                     EventBus& bus) {
    char id[32];
    std::snprintf(id, sizeof(id), "adapter_%u", idx);
    const std::string adapter_id = id;

    const double t_phase = static_cast<double>(tick_) * 0.25 + idx * 1.7;
    const double busy_pct = 50.0 + 40.0 * std::sin(t_phase);
    const double temp_c   = 55.0 + 5.0 * std::sin(t_phase * 0.5 + idx);
    const double power_w  = 80.0 + 15.0 * std::cos(t_phase * 0.3);
    const std::uint64_t vram_used =
        (12ull + (tick_ % 8) + idx) * 1024ull * 1024ull * 1024ull;

    const bool inject_nan = opts_.nan_injection_period &&
                            (tick_ % opts_.nan_injection_period) == (idx + 1);

    auto base = [&](std::string n, SemanticDomain d, Unit u) {
        MetricSample m;
        m.metric_name = std::move(n);
        m.adapter_id = adapter_id;
        m.session_epoch = session_epoch;
        m.semantic_domain = d;
        m.unit = u;
        m.source = Source::FakeCollector;
        m.timestamp_qpc = now_qpc_ns;
        m.poll_latency_ns = 1'000;
        m.sampling_window_ns = 0;
        m.observation_kind = ObservationKind::DirectlyObserved;
        m.correlation_method = CorrelationMethod::LUID_DirectBind;
        m.confidence = Confidence::High;
        m.flags = flags::SyntheticInjection;
        return m;
    };

    {
        auto m = base("vram.usage_bytes", SemanticDomain::Memory, Unit::Bytes);
        m.value = vram_used;
        bus.publish(m);
    }
    {
        auto m = base("engine.compute.busy_pct", SemanticDomain::EngineActivity, Unit::Percent);
        if (inject_nan) {
            m.value = std::numeric_limits<double>::quiet_NaN();
            m.flags |= flags::SourceEmittedNaN;
            m.confidence = Confidence::Low;
        } else {
            m.value = busy_pct;
            m.observation_kind = ObservationKind::DerivedFromDelta;
            m.sampling_window_ns = 1'000'000'000ull;
        }
        bus.publish(m);
    }
    {
        auto m = base("gpu.temperature_c", SemanticDomain::Thermal, Unit::Celsius);
        m.value = temp_c;
        bus.publish(m);
    }
    {
        auto m = base("gpu.power_w", SemanticDomain::Power, Unit::Watts);
        m.value = power_w;
        m.observation_kind = ObservationKind::DerivedFromDelta;
        m.sampling_window_ns = 1'000'000'000ull;
        bus.publish(m);
    }

    if (opts_.state_transition_period &&
        (tick_ > 0) &&
        (tick_ % opts_.state_transition_period) == 0) {
        AdapterState next = (busy_pct > 60.0) ? AdapterState::ActiveCompute
                                              : AdapterState::Awake;
        if (next != last_state_per_adapter_[idx]) {
            AdapterStateTransition t;
            t.adapter_id = adapter_id;
            t.from = last_state_per_adapter_[idx];
            t.to = next;
            t.reason = "synthetic-fake-collector-tick";
            t.session_epoch = session_epoch;
            t.timestamp_qpc = now_qpc_ns;
            bus.publish(t);
            last_state_per_adapter_[idx] = next;
        }
    }

    if (opts_.emit_synthetic_disagreement && idx == 0 && tick_ == 2) {
        MetricSample m = base("vram.total_bytes_via_taskmgr", SemanticDomain::Memory, Unit::Bytes);
        m.value = 48ull * 1024ull * 1024ull * 1024ull;
        m.source = Source::FakeCollector;
        m.source_detail = "synthetic-tm-48gb-pattern";
        m.observation_kind = ObservationKind::Reported_Untrusted;
        m.confidence = Confidence::Disagreed;
        bus.publish(m);
    }
}

void FakeCollector::poll(std::uint64_t now_qpc_ns,
                         std::uint32_t session_epoch,
                         EventBus& bus) {
    for (std::uint32_t i = 0; i < opts_.adapters; ++i) {
        if (last_state_per_adapter_[i] == AdapterState::Unknown) {
            AdapterStateTransition t;
            char id[32];
            std::snprintf(id, sizeof(id), "adapter_%u", i);
            t.adapter_id = id;
            t.from = AdapterState::Unknown;
            t.to = AdapterState::Idle;
            t.reason = "fake_collector:first-poll";
            t.session_epoch = session_epoch;
            t.timestamp_qpc = now_qpc_ns;
            bus.publish(t);
            last_state_per_adapter_[i] = AdapterState::Idle;
        }
        emit_one_adapter(i, now_qpc_ns, session_epoch, bus);
    }
    ++tick_;
}

}
