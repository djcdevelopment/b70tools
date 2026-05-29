#pragma once

#include "schema/events.h"
#include "schema/metric_sample.h"

#include <vector>

namespace b70 {

class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_metric(const MetricSample&) {}
    virtual void on_identity(const AdapterIdentity&) {}
    virtual void on_state_transition(const AdapterStateTransition&) {}
    virtual void on_disagreement(const DisagreementReport&) {}
    virtual void on_epoch_boundary(const SessionEpochBoundary&) {}
    virtual void on_fingerprint(const DriverRuntimeFingerprint&) {}
    virtual void on_audit(const CollectorAuditRecord&) {}
};

class EventBus {
public:
    void subscribe(EventSink* s);

    void publish(const MetricSample& m);
    void publish(const AdapterIdentity& a);
    void publish(const AdapterStateTransition& t);
    void publish(const DisagreementReport& d);
    void publish(const SessionEpochBoundary& e);
    void publish(const DriverRuntimeFingerprint& f);
    void publish(const CollectorAuditRecord& c);

private:
    std::vector<EventSink*> sinks_;
};

}
