#pragma once

#include "identity/dxgi_enum.h"
#include "identity/setupapi_devnodes.h"
#include "identity/vulkan_enum.h"
#include "schema/events.h"

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

struct BindingNote {
    std::string adapter_id;
    std::string severity;          // "info" | "warning" | "ambiguous"
    std::string explanation;
};

struct ReconciliationResult {
    std::vector<AdapterIdentity> adapters;
    std::vector<BindingNote> notes;
    bool ambiguous = false;        // true → caller should refuse to start polling
    bool dxgi_ok = false;
    bool setupapi_ok = false;
    bool vulkan_ok = false;
};

ReconciliationResult reconcile_identity(const DxgiEnumResult& dxgi,
                                        const SetupApiEnumResult& setup,
                                        const VulkanEnumResult& vk);

std::string format_driver_uuid_hex(const std::array<std::uint8_t, 16>& uuid);

}
