#pragma once

#include "bus/event_bus.h"
#include "schema/enums.h"

#include <string>
#include <unordered_map>

namespace b70 {

class AdapterStateFsm : public EventSink {
public:
    explicit AdapterStateFsm(EventBus* bus_for_emit = nullptr) : bus_(bus_for_emit) {}

    void on_state_transition(const AdapterStateTransition& t) override;
    void on_identity(const AdapterIdentity& a) override;
    void on_metric(const MetricSample& m) override;

    AdapterState current(const std::string& adapter_id) const;

private:
    EventBus* bus_;
    std::unordered_map<std::string, AdapterState> state_;

    void publish_transition(const std::string& adapter_id, AdapterState from, AdapterState to,
                            const std::string& reason, std::uint32_t epoch, std::uint64_t ts);
};

}
