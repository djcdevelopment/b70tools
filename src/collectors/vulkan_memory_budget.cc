#include "collectors/vulkan_memory_budget.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace b70 {

struct VulkanMemoryBudgetCollector::Impl {
    HMODULE dll = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkCreateInstance      createInstance = nullptr;
    PFN_vkDestroyInstance     destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2 = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties2 getPhysicalDeviceMemoryProperties2 = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensionProperties = nullptr;

    VkInstance instance = VK_NULL_HANDLE;

    struct Bound {
        std::string adapter_id;
        std::uint64_t luid_raw = 0;
        VkPhysicalDevice pd = VK_NULL_HANDLE;
        bool has_memory_budget = false;
        std::uint32_t heap_count = 0;
    };
    std::vector<Bound> bound;
    std::string init_error;
};

VulkanMemoryBudgetCollector::VulkanMemoryBudgetCollector() : impl_(new Impl) {}

VulkanMemoryBudgetCollector::~VulkanMemoryBudgetCollector() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

CollectorSideEffects VulkanMemoryBudgetCollector::declared_side_effects() const {
    CollectorSideEffects s;
    // FIRST-CLASS FINDING (recorded 2026-05-28): Vulkan init is not inert on this host.
    // Our code creates no VkDevice and allocates no GPU memory (app_passive=true),
    // but vkCreateInstance triggers Intel ICD load which pulls in ControlLib.dll,
    // IntelControlLib.dll, igc64.dll, igvk64.dll, RTSSVkLayer64.dll, and +17.5MB RSS.
    // Reclassified from TrulyPassive → DriverPassive to honor the audit observation.
    s.app_passive = true;
    s.may_trigger_driver_init = true;
    s.intrusiveness = Intrusiveness::DriverPassive;
    return s;
}

bool VulkanMemoryBudgetCollector::init(EventBus& /*bus*/,
                                       const std::vector<AdapterIdentity>& adapters) {
    auto& I = *impl_;
    I.dll = LoadLibraryW(L"vulkan-1.dll");
    if (!I.dll) { I.init_error = "vulkan-1.dll not loadable"; return false; }
    I.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(I.dll, "vkGetInstanceProcAddr"));
    if (!I.getInstanceProcAddr) { I.init_error = "vkGetInstanceProcAddr missing"; return false; }

    I.createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        I.getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!I.createInstance) { I.init_error = "vkCreateInstance missing"; return false; }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "b70tools";
    app.applicationVersion = 1;
    app.pEngineName = "b70tools";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    if (I.createInstance(&ici, nullptr, &I.instance) != VK_SUCCESS) {
        I.init_error = "vkCreateInstance failed";
        return false;
    }

    auto gip = [&](const char* nm) { return I.getInstanceProcAddr(I.instance, nm); };
    I.destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(gip("vkDestroyInstance"));
    I.enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gip("vkEnumeratePhysicalDevices"));
    I.getPhysicalDeviceProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(gip("vkGetPhysicalDeviceProperties2"));
    I.getPhysicalDeviceMemoryProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(gip("vkGetPhysicalDeviceMemoryProperties2"));
    I.enumerateDeviceExtensionProperties = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(gip("vkEnumerateDeviceExtensionProperties"));
    if (!I.destroyInstance || !I.enumeratePhysicalDevices ||
        !I.getPhysicalDeviceProperties2 || !I.getPhysicalDeviceMemoryProperties2 ||
        !I.enumerateDeviceExtensionProperties) {
        I.init_error = "instance entry points missing";
        return false;
    }

    std::uint32_t count = 0;
    I.enumeratePhysicalDevices(I.instance, &count, nullptr);
    std::vector<VkPhysicalDevice> pds(count);
    if (count > 0) I.enumeratePhysicalDevices(I.instance, &count, pds.data());

    for (auto pd : pds) {
        VkPhysicalDeviceIDProperties idp{};
        idp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        VkPhysicalDeviceProperties2 p2{};
        p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        p2.pNext = &idp;
        I.getPhysicalDeviceProperties2(pd, &p2);

        if (!idp.deviceLUIDValid) continue;

        std::uint32_t lo = 0;
        std::int32_t  hi = 0;
        std::memcpy(&lo, idp.deviceLUID + 0, 4);
        std::memcpy(&hi, idp.deviceLUID + 4, 4);
        const std::uint64_t raw =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(hi)) << 32)
            | static_cast<std::uint64_t>(lo);

        for (const auto& a : adapters) {
            if (a.luid != raw) continue;

            std::uint32_t ext_count = 0;
            I.enumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            if (ext_count) I.enumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
            bool has_budget = false;
            for (auto& e : exts) {
                if (std::strcmp(e.extensionName, "VK_EXT_memory_budget") == 0) {
                    has_budget = true;
                    break;
                }
            }

            VkPhysicalDeviceMemoryProperties2 mp2{};
            mp2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
            I.getPhysicalDeviceMemoryProperties2(pd, &mp2);
            Impl::Bound b;
            b.adapter_id = a.adapter_id;
            b.luid_raw = raw;
            b.pd = pd;
            b.has_memory_budget = has_budget;
            b.heap_count = mp2.memoryProperties.memoryHeapCount;
            I.bound.push_back(std::move(b));
            break;
        }
    }
    return !I.bound.empty();
}

void VulkanMemoryBudgetCollector::shutdown() {
    auto& I = *impl_;
    if (I.destroyInstance && I.instance != VK_NULL_HANDLE) {
        I.destroyInstance(I.instance, nullptr);
        I.instance = VK_NULL_HANDLE;
    }
    I.bound.clear();
    if (I.dll) { FreeLibrary(I.dll); I.dll = nullptr; }
    I.getInstanceProcAddr = nullptr;
    I.createInstance = nullptr;
    I.destroyInstance = nullptr;
    I.enumeratePhysicalDevices = nullptr;
    I.getPhysicalDeviceProperties2 = nullptr;
    I.getPhysicalDeviceMemoryProperties2 = nullptr;
    I.enumerateDeviceExtensionProperties = nullptr;
}

void VulkanMemoryBudgetCollector::poll(std::uint64_t now_qpc_ns,
                                       std::uint32_t session_epoch,
                                       EventBus& bus) {
    auto& I = *impl_;
    for (auto& b : I.bound) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT bp{};
        bp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;

        VkPhysicalDeviceMemoryProperties2 mp2{};
        mp2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
        if (b.has_memory_budget) mp2.pNext = &bp;

        I.getPhysicalDeviceMemoryProperties2(b.pd, &mp2);

        const auto& m = mp2.memoryProperties;
        for (std::uint32_t i = 0; i < m.memoryHeapCount; ++i) {
            auto base = [&](std::string nm) {
                MetricSample s;
                s.metric_name = std::move(nm);
                s.adapter_id = b.adapter_id;
                s.session_epoch = session_epoch;
                s.semantic_domain = SemanticDomain::Memory;
                s.unit = Unit::Bytes;
                s.source = Source::Vulkan_MemoryBudget;
                s.timestamp_qpc = now_qpc_ns;
                s.poll_latency_ns = 0;
                s.sampling_window_ns = 0;
                s.observation_kind = ObservationKind::DirectlyObserved;
                s.correlation_method = CorrelationMethod::ProcessID_Filter;  // per-process
                s.confidence = Confidence::High;
                return s;
            };

            char nm[64];
            std::snprintf(nm, sizeof(nm), "vulkan.heap%u.size_bytes", i);
            {
                auto s = base(nm);
                s.value = static_cast<std::uint64_t>(m.memoryHeaps[i].size);
                s.observation_kind = ObservationKind::DirectlyObserved;
                bus.publish(s);
            }
            if (b.has_memory_budget) {
                std::snprintf(nm, sizeof(nm), "vulkan.heap%u.budget_bytes", i);
                {
                    auto s = base(nm);
                    s.value = static_cast<std::uint64_t>(bp.heapBudget[i]);
                    bus.publish(s);
                }
                std::snprintf(nm, sizeof(nm), "vulkan.heap%u.usage_bytes", i);
                {
                    auto s = base(nm);
                    s.value = static_cast<std::uint64_t>(bp.heapUsage[i]);
                    bus.publish(s);
                }
            }
        }
    }
}

}
