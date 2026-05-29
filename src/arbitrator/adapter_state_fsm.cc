#include "arbitrator/adapter_state_fsm.h"

#include "schema/events.h"
#include "schema/metric_sample.h"

#include <cmath>
#include <variant>

namespace b70 {

namespace {

bool value_is_meaningful(const MetricValue& v) {
    if (std::holds_alternative<ValueMissing>(v)) return false;
    if (std::holds_alternative<std::uint64_t>(v)) return true;  // any successful u64 read
    if (std::holds_alternative<std::int64_t>(v)) return true;
    if (std::holds_alternative<double>(v)) {
        double d = std::get<double>(v);
        return std::isfinite(d);
    }
    return false;
}

}

void AdapterStateFsm::on_state_transition(const AdapterStateTransition& t) {
    state_[t.adapter_id] = t.to;
}

void AdapterStateFsm::on_identity(const AdapterIdentity& a) {
    auto it = state_.find(a.adapter_id);
    if (it == state_.end()) state_[a.adapter_id] = AdapterState::Unknown;
}

void AdapterStateFsm::on_metric(const MetricSample& m) {
    if (!bus_) return;
    if (m.flags & flags::SyntheticInjection) return;
    if (m.adapter_id.empty()) return;

    const AdapterState cur = current(m.adapter_id);
    if (cur != AdapterState::Unknown) return;  // M2 scope: only Unknown→Idle transition

    const bool relevant_domain =
        m.semantic_domain == SemanticDomain::Power ||
        m.semantic_domain == SemanticDomain::Frequency ||
        m.semantic_domain == SemanticDomain::Thermal ||
        m.semantic_domain == SemanticDomain::EngineActivity ||
        m.semantic_domain == SemanticDomain::Memory;
    if (!relevant_domain) return;
    if (!value_is_meaningful(m.value)) return;

    publish_transition(m.adapter_id, AdapterState::Unknown, AdapterState::Idle,
                       std::string("fsm:first-evidence(domain=") + std::string(to_string(m.semantic_domain)) +
                           ",source=" + std::string(to_string(m.source)) + ")",
                       m.session_epoch, m.timestamp_qpc);
}

void AdapterStateFsm::publish_transition(const std::string& adapter_id,
                                          AdapterState from, AdapterState to,
                                          const std::string& reason,
                                          std::uint32_t epoch, std::uint64_t ts) {
    if (!bus_) return;
    AdapterStateTransition t;
    t.adapter_id = adapter_id;
    t.from = from;
    t.to = to;
    t.reason = reason;
    t.session_epoch = epoch;
    t.timestamp_qpc = ts;
    bus_->publish(t);
}

AdapterState AdapterStateFsm::current(const std::string& adapter_id) const {
    auto it = state_.find(adapter_id);
    return it == state_.end() ? AdapterState::Unknown : it->second;
}

}
