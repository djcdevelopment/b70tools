#include "bus/event_bus.h"

namespace b70 {

void EventBus::subscribe(EventSink* s) {
    if (s) sinks_.push_back(s);
}

void EventBus::publish(const MetricSample& m) {
    for (auto* s : sinks_) s->on_metric(m);
}
void EventBus::publish(const AdapterIdentity& a) {
    for (auto* s : sinks_) s->on_identity(a);
}
void EventBus::publish(const AdapterStateTransition& t) {
    for (auto* s : sinks_) s->on_state_transition(t);
}
void EventBus::publish(const DisagreementReport& d) {
    for (auto* s : sinks_) s->on_disagreement(d);
}
void EventBus::publish(const SessionEpochBoundary& e) {
    for (auto* s : sinks_) s->on_epoch_boundary(e);
}
void EventBus::publish(const DriverRuntimeFingerprint& f) {
    for (auto* s : sinks_) s->on_fingerprint(f);
}
void EventBus::publish(const CollectorAuditRecord& c) {
    for (auto* s : sinks_) s->on_audit(c);
}

}
