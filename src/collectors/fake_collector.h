#pragma once

#include "collectors/collector.h"
#include "schema/enums.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

class FakeCollector : public Collector {
public:
    struct Options {
        std::uint32_t adapters = 2;
        std::uint64_t nan_injection_period = 7;
        std::uint64_t state_transition_period = 3;
        bool emit_synthetic_disagreement = true;
    };

    explicit FakeCollector(Options o = {});

    const char* name() const override { return "fake_collector"; }
    CollectorSideEffects declared_side_effects() const override;

    bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) override;
    void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus& bus) override;

private:
    Options opts_;
    std::uint64_t tick_ = 0;
    std::vector<AdapterState> last_state_per_adapter_;

    void emit_one_adapter(std::uint32_t idx,
                          std::uint64_t now_qpc_ns,
                          std::uint32_t session_epoch,
                          EventBus& bus);
};

}
