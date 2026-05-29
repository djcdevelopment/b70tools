#include "runtime/driver_fingerprint.h"

#include "runtime/session.h"
#include "schema/enums.h"
#include "schema/metric_sample.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace b70 {

namespace {

bool reg_get_string(HKEY root, const wchar_t* subkey, const wchar_t* value,
                    std::string& out_utf8) {
    HKEY h{};
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return false;
    wchar_t buf[512];
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    LONG r = RegQueryValueExW(h, value, nullptr, &type,
                              reinterpret_cast<LPBYTE>(buf), &sz);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return false;
    out_utf8.assign(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out_utf8.data(), n, nullptr, nullptr);
    return true;
}

bool reg_get_dword(HKEY root, const wchar_t* subkey, const wchar_t* value,
                   std::uint32_t& out) {
    HKEY h{};
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &h) != ERROR_SUCCESS) return false;
    DWORD v = 0;
    DWORD sz = sizeof(v);
    DWORD type = 0;
    LONG r = RegQueryValueExW(h, value, nullptr, &type,
                              reinterpret_cast<LPBYTE>(&v), &sz);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || type != REG_DWORD) return false;
    out = v;
    return true;
}

std::string decode_intel_driver_uuid_ascii(const std::string& uuid_hex) {
    // Intel encodes their Windows driver version as the deviceUUID, packed as ASCII bytes.
    // uuid_hex is "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" (36 chars).
    std::string raw;
    for (char c : uuid_hex) {
        if (c == '-') continue;
        raw.push_back(c);
    }
    if (raw.size() != 32) return {};
    std::string out;
    for (std::size_t i = 0; i < raw.size(); i += 2) {
        unsigned v = 0;
        if (std::sscanf(raw.c_str() + i, "%2x", &v) != 1) return {};
        if (v == 0) break;
        if (v < 0x20 || v > 0x7e) return {};
        out.push_back(static_cast<char>(v));
    }
    return out;
}

void emit_u64(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
              std::uint64_t ts, std::string name, SemanticDomain d, Unit u,
              std::uint64_t v, Source src) {
    MetricSample m;
    m.metric_name = std::move(name);
    m.adapter_id = adapter_id;
    m.session_epoch = epoch;
    m.semantic_domain = d;
    m.unit = u;
    m.source = src;
    m.timestamp_qpc = ts;
    m.poll_latency_ns = 0;
    m.sampling_window_ns = 0;
    m.observation_kind = ObservationKind::DirectlyObserved;
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence = Confidence::High;
    m.value = v;
    bus.publish(m);
}

void emit_f64(EventBus& bus, const std::string& adapter_id, std::uint32_t epoch,
              std::uint64_t ts, std::string name, SemanticDomain d, Unit u,
              double v, Source src) {
    MetricSample m;
    m.metric_name = std::move(name);
    m.adapter_id = adapter_id;
    m.session_epoch = epoch;
    m.semantic_domain = d;
    m.unit = u;
    m.source = src;
    m.timestamp_qpc = ts;
    m.poll_latency_ns = 0;
    m.sampling_window_ns = 0;
    m.observation_kind = ObservationKind::Inferred;
    m.correlation_method = CorrelationMethod::LUID_DirectBind;
    m.confidence = Confidence::High;
    m.value = v;
    bus.publish(m);
}

const VulkanAdapterRecord* find_vk(const VulkanEnumResult& vk, std::uint64_t luid) {
    for (const auto& v : vk.adapters) {
        if (v.luid_valid && v.luid_raw == luid) return &v;
    }
    return nullptr;
}

const SetupApiDevnodeRecord* find_setup(const SetupApiEnumResult& setup, std::uint64_t luid) {
    for (const auto& s : setup.devnodes) {
        if (s.luid_present && s.luid_raw == luid) return &s;
    }
    return nullptr;
}

}

void publish_driver_fingerprint(EventBus& bus,
                                const std::vector<AdapterIdentity>& adapters,
                                const DxgiEnumResult& /*dxgi*/,
                                const SetupApiEnumResult& /*setup*/,
                                const VulkanEnumResult& vk,
                                std::uint32_t session_epoch) {
    DriverRuntimeFingerprint f;
    f.session_epoch = session_epoch;
    f.timestamp_qpc = Session::now_qpc_ns();

    std::string win_name, win_build, win_ubr;
    reg_get_string(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                   L"ProductName", win_name);
    reg_get_string(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                   L"CurrentBuildNumber", win_build);
    std::uint32_t ubr_dw = 0;
    if (reg_get_dword(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      L"UBR", ubr_dw)) {
        char tmp[16];
        std::snprintf(tmp, sizeof(tmp), "%u", ubr_dw);
        win_ubr = tmp;
    }
    f.windows_build = win_name + " 10.0." + win_build + (win_ubr.empty() ? "" : "." + win_ubr);

    reg_get_string(HKEY_LOCAL_MACHINE,
                   L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                   L"BIOSVersion", f.bios_version);
    reg_get_string(HKEY_LOCAL_MACHINE,
                   L"HARDWARE\\DESCRIPTION\\System\\BIOS",
                   L"BIOSReleaseDate", f.bios_date);

    std::uint32_t hags = 0;
    if (reg_get_dword(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers",
                      L"HwSchMode", hags)) {
        f.hags_enabled = (hags >= 1);
    }

    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "vulkan_loader_api=%u.%u.%u",
                  VK_VERSION_MAJOR(vk.instance_api_version),
                  VK_VERSION_MINOR(vk.instance_api_version),
                  VK_VERSION_PATCH(vk.instance_api_version));
    f.vulkan_runtime_version = tmp;

    if (!adapters.empty()) {
        const std::string& uuid_hex = adapters.front().driver_uuid;
        const std::string decoded = decode_intel_driver_uuid_ascii(uuid_hex);
        if (!decoded.empty()) {
            f.intel_driver_version = decoded;
        } else {
            f.intel_driver_version = std::string("uuid=") + uuid_hex;
        }
    }

    // Per-adapter notes + per-adapter MetricSamples.
    char note_buf[256];
    for (const auto& a : adapters) {
        const VulkanAdapterRecord* v = find_vk(vk, a.luid);

        std::uint64_t cpu_visible = v ? v->largest_device_local_host_visible_bytes : 0;
        double cpu_visible_pct = 0.0;
        bool rebar_active = false;
        if (v && a.dedicated_video_memory > 0) {
            cpu_visible_pct = 100.0 * static_cast<double>(cpu_visible)
                              / static_cast<double>(a.dedicated_video_memory);
            rebar_active = cpu_visible_pct >= 95.0;
        }

        // PCIe probe via SetupAPI display-class devnodes returns unreliable values
        // on this host (likely needs parent PCI devnode walk). Defer proper probe to v1.5.
        std::snprintf(note_buf, sizeof(note_buf),
                      "%s: cpu_visible_vram=%.1f%% rebar=%s (pcie_probe=v1.5)",
                      a.adapter_id.c_str(), cpu_visible_pct,
                      rebar_active ? "active" : "capped");
        f.per_adapter_notes.emplace_back(note_buf);

        emit_u64(bus, a.adapter_id, session_epoch, f.timestamp_qpc,
                 "gpu.cpu_visible_vram_bytes", SemanticDomain::Memory, Unit::Bytes,
                 cpu_visible, Source::Vulkan_Enumeration);
        emit_f64(bus, a.adapter_id, session_epoch, f.timestamp_qpc,
                 "gpu.cpu_visible_vram_pct", SemanticDomain::Memory, Unit::Percent,
                 cpu_visible_pct, Source::Vulkan_Enumeration);
        emit_u64(bus, a.adapter_id, session_epoch, f.timestamp_qpc,
                 "gpu.rebar_active", SemanticDomain::PCIe, Unit::Dimensionless,
                 rebar_active ? 1u : 0u, Source::Vulkan_Enumeration);
    }

    bus.publish(f);
}

}
