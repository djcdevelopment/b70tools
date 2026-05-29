#pragma once

#include "schema/enums.h"
#include "schema/metric_sample.h"
#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

struct AdapterIdentity {
    std::string adapter_id;
    std::uint64_t luid = 0;
    std::string description;
    std::uint64_t dedicated_video_memory = 0;
    std::uint64_t shared_system_memory = 0;
    std::string pci_bdf;
    std::string driver_uuid;
    std::uint32_t session_epoch = 0;
    std::uint64_t timestamp_qpc = 0;
    std::vector<std::string> bindings;
};

struct AdapterStateTransition {
    std::string adapter_id;
    AdapterState from = AdapterState::Unknown;
    AdapterState to = AdapterState::Unknown;
    std::string reason;
    std::uint32_t session_epoch = 0;
    std::uint64_t timestamp_qpc = 0;
};

struct DisagreementReport {
    std::string rule_name;
    std::string adapter_id;
    std::string explanation;
    Confidence resulting_confidence = Confidence::Disagreed;
    std::vector<std::string> involved_sources;
    std::uint32_t session_epoch = 0;
    std::uint64_t timestamp_qpc = 0;
};

struct SessionEpochBoundary {
    std::uint32_t previous_epoch = 0;
    std::uint32_t new_epoch = 0;
    std::string cause;
    std::uint64_t timestamp_qpc = 0;
};

struct DriverRuntimeFingerprint {
    std::string windows_build;
    std::string bios_version;
    std::string bios_date;
    std::string intel_driver_version;
    std::string vulkan_runtime_version;
    bool hags_enabled = false;
    bool wdac_enforced = false;
    bool above_4g_decoding = false;
    std::uint32_t session_epoch = 0;
    std::uint64_t timestamp_qpc = 0;
    std::vector<std::string> per_adapter_notes;
};

struct CollectorAuditRecord {
    std::string collector_name;
    std::uint32_t threads_before = 0;
    std::uint32_t threads_after = 0;
    std::vector<std::string> modules_loaded;
    std::uint64_t init_wall_ns = 0;
    std::int64_t rss_delta_bytes = 0;
    bool declared_truly_passive = false;        // declared TrulyPassive
    bool declared_driver_passive = false;       // declared DriverPassive
    bool observed_undeclared_side_effects = false;
    std::string notes;
    std::uint32_t session_epoch = 0;
    std::uint64_t timestamp_qpc = 0;
};

}
