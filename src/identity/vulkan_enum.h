#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

struct VulkanHeap {
    std::uint64_t size = 0;
    std::uint32_t flags = 0;             // VkMemoryHeapFlags
};

struct VulkanMemoryType {
    std::uint32_t property_flags = 0;    // VkMemoryPropertyFlags
    std::uint32_t heap_index = 0;
};

struct VulkanAdapterRecord {
    std::uint32_t vk_index = 0;
    std::string  device_name;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t api_version = 0;
    std::uint32_t driver_version = 0;
    std::uint32_t device_type = 0;       // VkPhysicalDeviceType

    std::array<std::uint8_t, 16> device_uuid{};
    std::array<std::uint8_t, 16> driver_uuid{};
    std::array<std::uint8_t, 8>  device_luid{};
    bool         luid_valid = false;
    std::uint32_t device_node_mask = 0;

    std::uint64_t luid_raw = 0;          // host endian; same packing as Windows LUID

    std::vector<VulkanHeap>       heaps;
    std::vector<VulkanMemoryType> memory_types;
    std::uint64_t total_device_local_bytes = 0;
    std::uint64_t largest_device_local_host_visible_bytes = 0;

    std::vector<std::string> extensions;
    bool has_memory_budget_ext = false;
    bool has_pci_bus_info_ext = false;
    bool has_calibrated_timestamps_ext = false;
};

struct VulkanEnumResult {
    bool loader_present = false;
    bool instance_created = false;
    std::uint32_t instance_api_version = 0;
    std::string error;
    std::vector<VulkanAdapterRecord> adapters;
};

VulkanEnumResult enumerate_vulkan_adapters();

}
