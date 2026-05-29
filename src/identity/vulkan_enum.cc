#include "identity/vulkan_enum.h"

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace b70 {

namespace {

struct VkApi {
    HMODULE dll = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;

    // Global-scope
    PFN_vkEnumerateInstanceVersion enumerateInstanceVersion = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties enumerateInstanceExtensionProperties = nullptr;

    // Instance-scope
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2 getPhysicalDeviceMemoryProperties2 = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties = nullptr;

    bool load_dll() {
        dll = LoadLibraryW(L"vulkan-1.dll");
        if (!dll) return false;
        getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(dll, "vkGetInstanceProcAddr"));
        return getInstanceProcAddr != nullptr;
    }

    void unload() {
        if (dll) { FreeLibrary(dll); dll = nullptr; }
    }

    template <typename Fn>
    Fn get_instance_proc(VkInstance inst, const char* name) {
        return reinterpret_cast<Fn>(getInstanceProcAddr(inst, name));
    }

    bool load_globals() {
        createInstance = get_instance_proc<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
        enumerateInstanceExtensionProperties = get_instance_proc<PFN_vkEnumerateInstanceExtensionProperties>(
            VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
        enumerateInstanceVersion = get_instance_proc<PFN_vkEnumerateInstanceVersion>(
            VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
        return createInstance != nullptr;
    }

    bool load_instance(VkInstance inst) {
        destroyInstance = get_instance_proc<PFN_vkDestroyInstance>(inst, "vkDestroyInstance");
        enumeratePhysicalDevices = get_instance_proc<PFN_vkEnumeratePhysicalDevices>(
            inst, "vkEnumeratePhysicalDevices");
        getPhysicalDeviceProperties2 = get_instance_proc<PFN_vkGetPhysicalDeviceProperties2>(
            inst, "vkGetPhysicalDeviceProperties2");
        getPhysicalDeviceMemoryProperties2 = get_instance_proc<PFN_vkGetPhysicalDeviceMemoryProperties2>(
            inst, "vkGetPhysicalDeviceMemoryProperties2");
        enumerateDeviceExtensionProperties = get_instance_proc<PFN_vkEnumerateDeviceExtensionProperties>(
            inst, "vkEnumerateDeviceExtensionProperties");
        return destroyInstance && enumeratePhysicalDevices &&
               getPhysicalDeviceProperties2 && getPhysicalDeviceMemoryProperties2 &&
               enumerateDeviceExtensionProperties;
    }
};

void extract_one_adapter(VkApi& v, VkPhysicalDevice pd, std::uint32_t idx,
                         VulkanAdapterRecord& out) {
    out.vk_index = idx;

    VkPhysicalDeviceIDProperties idp{};
    idp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &idp;
    v.getPhysicalDeviceProperties2(pd, &props2);

    const auto& p = props2.properties;
    out.device_name    = p.deviceName;
    out.vendor_id      = p.vendorID;
    out.device_id      = p.deviceID;
    out.api_version    = p.apiVersion;
    out.driver_version = p.driverVersion;
    out.device_type    = static_cast<std::uint32_t>(p.deviceType);

    std::memcpy(out.device_uuid.data(), idp.deviceUUID, 16);
    std::memcpy(out.driver_uuid.data(), idp.driverUUID, 16);
    std::memcpy(out.device_luid.data(), idp.deviceLUID, 8);
    out.luid_valid = (idp.deviceLUIDValid == VK_TRUE);
    out.device_node_mask = idp.deviceNodeMask;

    if (out.luid_valid) {
        std::uint32_t low  = 0;
        std::int32_t  high = 0;
        std::memcpy(&low,  out.device_luid.data() + 0, 4);
        std::memcpy(&high, out.device_luid.data() + 4, 4);
        out.luid_raw = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32)
                       | static_cast<std::uint64_t>(low);
    }

    VkPhysicalDeviceMemoryProperties2 mp2{};
    mp2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    v.getPhysicalDeviceMemoryProperties2(pd, &mp2);
    const auto& m = mp2.memoryProperties;

    out.heaps.reserve(m.memoryHeapCount);
    for (std::uint32_t i = 0; i < m.memoryHeapCount; ++i) {
        out.heaps.push_back({m.memoryHeaps[i].size, m.memoryHeaps[i].flags});
        if (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            out.total_device_local_bytes += m.memoryHeaps[i].size;
        }
    }
    out.memory_types.reserve(m.memoryTypeCount);
    for (std::uint32_t i = 0; i < m.memoryTypeCount; ++i) {
        out.memory_types.push_back({m.memoryTypes[i].propertyFlags, m.memoryTypes[i].heapIndex});
        const auto pf = m.memoryTypes[i].propertyFlags;
        const bool dl = (pf & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        const bool hv = (pf & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        if (dl && hv) {
            const std::uint32_t hi = m.memoryTypes[i].heapIndex;
            if (hi < m.memoryHeapCount) {
                const std::uint64_t sz = m.memoryHeaps[hi].size;
                if (sz > out.largest_device_local_host_visible_bytes) {
                    out.largest_device_local_host_visible_bytes = sz;
                }
            }
        }
    }

    std::uint32_t ext_count = 0;
    v.enumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    if (ext_count > 0) {
        v.enumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
    }
    out.extensions.reserve(ext_count);
    for (auto& e : exts) {
        out.extensions.emplace_back(e.extensionName);
        if (std::strcmp(e.extensionName, "VK_EXT_memory_budget") == 0)
            out.has_memory_budget_ext = true;
        if (std::strcmp(e.extensionName, "VK_EXT_pci_bus_info") == 0)
            out.has_pci_bus_info_ext = true;
        if (std::strcmp(e.extensionName, "VK_KHR_calibrated_timestamps") == 0)
            out.has_calibrated_timestamps_ext = true;
    }
}

}

VulkanEnumResult enumerate_vulkan_adapters() {
    VulkanEnumResult out;

    VkApi v;
    if (!v.load_dll()) {
        out.error = "vulkan-1.dll not loadable";
        return out;
    }
    out.loader_present = true;
    if (!v.load_globals()) {
        out.error = "vkCreateInstance not exported by loader";
        v.unload();
        return out;
    }

    if (v.enumerateInstanceVersion) {
        v.enumerateInstanceVersion(&out.instance_api_version);
    } else {
        out.instance_api_version = VK_API_VERSION_1_0;
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "b70tools";
    app.applicationVersion = 1;
    app.pEngineName = "b70tools";
    app.engineVersion = 1;
    app.apiVersion = (out.instance_api_version >= VK_API_VERSION_1_1)
        ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = v.createInstance(&ici, nullptr, &inst);
    if (r != VK_SUCCESS) {
        char tmp[80];
        std::snprintf(tmp, sizeof(tmp), "vkCreateInstance failed: VkResult %d", static_cast<int>(r));
        out.error = tmp;
        v.unload();
        return out;
    }
    out.instance_created = true;

    if (!v.load_instance(inst)) {
        out.error = "could not load required instance-level entry points";
        if (v.destroyInstance) v.destroyInstance(inst, nullptr);
        v.unload();
        return out;
    }

    std::uint32_t count = 0;
    v.enumeratePhysicalDevices(inst, &count, nullptr);
    std::vector<VkPhysicalDevice> pds(count);
    if (count > 0) v.enumeratePhysicalDevices(inst, &count, pds.data());

    out.adapters.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        VulkanAdapterRecord rec;
        extract_one_adapter(v, pds[i], i, rec);
        out.adapters.push_back(std::move(rec));
    }

    v.destroyInstance(inst, nullptr);
    v.unload();
    return out;
}

}
