#include "identity/reconciler.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

namespace b70 {

namespace {

std::string adapter_id_from_luid(std::uint64_t luid_raw) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "adapter_%08x", static_cast<std::uint32_t>(luid_raw & 0xFFFFFFFFu));
    return buf;
}

std::string hex16(std::uint64_t v) {
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

}

std::string format_driver_uuid_hex(const std::array<std::uint8_t, 16>& uuid) {
    char buf[40];
    std::snprintf(buf, sizeof(buf),
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  uuid[0], uuid[1], uuid[2], uuid[3],
                  uuid[4], uuid[5],
                  uuid[6], uuid[7],
                  uuid[8], uuid[9],
                  uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    return buf;
}

ReconciliationResult reconcile_identity(const DxgiEnumResult& dxgi,
                                        const SetupApiEnumResult& setup,
                                        const VulkanEnumResult& vk) {
    ReconciliationResult out;
    out.dxgi_ok = dxgi.error.empty() && !dxgi.adapters.empty();
    out.setupapi_ok = setup.error.empty();
    out.vulkan_ok = vk.error.empty() && vk.instance_created;

    std::unordered_set<std::uint64_t> seen_luids;
    for (const auto& a : dxgi.adapters) {
        if (a.is_software || a.is_remote) continue;
        if (!seen_luids.insert(a.luid_raw).second) {
            out.ambiguous = true;
            out.notes.push_back({"<duplicate>", "ambiguous",
                                 "DXGI reported the same AdapterLuid twice across enumeration"});
        }
    }

    std::unordered_map<std::uint64_t, const VulkanAdapterRecord*> vk_by_luid;
    for (const auto& v : vk.adapters) {
        if (v.luid_valid) vk_by_luid[v.luid_raw] = &v;
    }

    std::unordered_map<std::uint64_t, const SetupApiDevnodeRecord*> setup_by_luid;
    for (const auto& s : setup.devnodes) {
        if (s.luid_present) setup_by_luid[s.luid_raw] = &s;
    }

    for (const auto& d : dxgi.adapters) {
        if (d.is_software || d.is_remote) continue;

        AdapterIdentity id;
        id.adapter_id              = adapter_id_from_luid(d.luid_raw);
        id.luid                    = d.luid_raw;
        id.description             = d.description_utf8;
        id.dedicated_video_memory  = d.dedicated_video_memory;
        id.shared_system_memory    = d.shared_system_memory;
        id.timestamp_qpc           = 0;

        char tmp[160];
        std::snprintf(tmp, sizeof(tmp),
                      "DXGI:idx=%u vendor=0x%04x device=0x%04x luid=0x%s desc=\"%s\"",
                      d.dxgi_index, d.vendor_id, d.device_id, hex16(d.luid_raw).c_str(),
                      d.description_utf8.c_str());
        id.bindings.emplace_back(tmp);

        auto sit = setup_by_luid.find(d.luid_raw);
        if (sit != setup_by_luid.end()) {
            const auto* s = sit->second;
            id.pci_bdf = s->pci_bdf;
            std::snprintf(tmp, sizeof(tmp),
                          "SetupAPI:bdf=%s loc=\"%s\" inst=\"%s\"",
                          s->pci_bdf.c_str(),
                          s->location_info_utf8.c_str(),
                          s->device_instance_id.c_str());
            id.bindings.emplace_back(tmp);
        } else {
            out.notes.push_back({id.adapter_id, "warning",
                                 "no SetupAPI display devnode matched this LUID; PCI BDF unavailable"});
        }

        auto vit = vk_by_luid.find(d.luid_raw);
        if (vit != vk_by_luid.end()) {
            const auto* v = vit->second;
            id.driver_uuid = format_driver_uuid_hex(v->driver_uuid);
            std::snprintf(tmp, sizeof(tmp),
                          "Vulkan:idx=%u devname=\"%s\" api=%u.%u.%u driver_uuid=%s "
                          "ext_memory_budget=%d pci_bus_info=%d",
                          v->vk_index, v->device_name.c_str(),
                          VK_VERSION_MAJOR(v->api_version),
                          VK_VERSION_MINOR(v->api_version),
                          VK_VERSION_PATCH(v->api_version),
                          id.driver_uuid.c_str(),
                          v->has_memory_budget_ext ? 1 : 0,
                          v->has_pci_bus_info_ext ? 1 : 0);
            id.bindings.emplace_back(tmp);

            std::snprintf(tmp, sizeof(tmp),
                          "VulkanHeapLayout:device_local=%llu B, largest_dl_hostvis=%llu B",
                          static_cast<unsigned long long>(v->total_device_local_bytes),
                          static_cast<unsigned long long>(v->largest_device_local_host_visible_bytes));
            id.bindings.emplace_back(tmp);
        } else if (out.vulkan_ok) {
            out.notes.push_back({id.adapter_id, "warning",
                                 "no Vulkan physical device matched this LUID; Vulkan-side bindings missing"});
        }

        out.adapters.push_back(std::move(id));
    }

    for (const auto& v : vk.adapters) {
        if (!v.luid_valid) {
            out.notes.push_back({"<vk-noluid>", "warning",
                                 "Vulkan physical device exposes invalid deviceLUID — spec violation; not bound"});
            continue;
        }
        bool matched = false;
        for (const auto& d : dxgi.adapters) {
            if (d.luid_raw == v.luid_raw) { matched = true; break; }
        }
        if (!matched) {
            out.notes.push_back({adapter_id_from_luid(v.luid_raw), "warning",
                                 "Vulkan adapter has LUID not present in DXGI enumeration"});
        }
    }

    return out;
}

}
