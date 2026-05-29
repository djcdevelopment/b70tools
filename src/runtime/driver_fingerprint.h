#pragma once

#include "bus/event_bus.h"
#include "identity/dxgi_enum.h"
#include "identity/setupapi_devnodes.h"
#include "identity/vulkan_enum.h"
#include "schema/events.h"

#include <cstdint>
#include <vector>

namespace b70 {

// Reads BIOS/HAGS/TDR/Windows registry; decodes Intel driver_uuid; computes ReBAR + CPU-visible-VRAM
// per adapter; publishes one DriverRuntimeFingerprint event and per-adapter PCIe/Memory MetricSamples.
// Called once at session start. Read-only; never touches the GPU.
void publish_driver_fingerprint(EventBus& bus,
                                const std::vector<AdapterIdentity>& adapters,
                                const DxgiEnumResult& dxgi,
                                const SetupApiEnumResult& setup,
                                const VulkanEnumResult& vk,
                                std::uint32_t session_epoch);

}
