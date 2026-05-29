#pragma once

#include "bus/event_bus.h"
#include "schema/events.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

// Three-axis passivity taxonomy. Each collector declares all three:
//   app_passive    — b70tools' own code performs no active GPU work.
//   driver_passive — the API used may still cause driver/runtime initialization.
//   intrusiveness  — overall summary classification (declared by author, verified by audit).
//
// Audit detects "truly passive in practice" empirically (0 modules / 0 threads / tiny RSS).
// A collector can be declared AppPassive=true but be empirically not-TrulyPassive
// if its underlying API pulls in vendor stacks (e.g. vulkan_memory_budget loads IGCL).
enum class Intrusiveness : std::uint8_t {
    TrulyPassive,     // expected: 0 threads, 0 new modules, tiny RSS delta, no power-state effects
    DriverPassive,    // app code is passive but the API triggers driver/runtime init
    Probe,            // active read against the device that may briefly contend with workload
    Intrusive,        // creates GPU contexts, allocates GPU memory, structural change to system
};

struct CollectorSideEffects {
    bool app_passive = true;                 // our code performs no active GPU work
    bool may_trigger_driver_init = false;    // API may pull in vendor DLLs / runtime
    bool creates_vk_device = false;
    bool creates_vk_queue = false;
    bool creates_command_pool = false;
    bool allocates_gpu_memory = false;
    bool spawns_threads_on_init = false;
    bool may_wake_idle_adapter = false;
    bool may_alter_power_state = false;
    bool may_alter_clocks = false;
    bool may_increase_residency = false;
    bool triggers_driver_refresh = false;
    Intrusiveness intrusiveness = Intrusiveness::TrulyPassive;
};

class Collector {
public:
    virtual ~Collector() = default;
    virtual const char* name() const = 0;
    virtual CollectorSideEffects declared_side_effects() const = 0;
    virtual bool init(EventBus& bus, const std::vector<AdapterIdentity>& adapters) {
        (void)bus; (void)adapters;
        return true;
    }
    virtual void poll(std::uint64_t now_qpc_ns, std::uint32_t session_epoch, EventBus&) = 0;
    virtual void shutdown() {}
};

}
