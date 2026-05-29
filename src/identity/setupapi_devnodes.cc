#include "identity/setupapi_devnodes.h"

#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>
#include <setupapi.h>

#include <cstdio>
#include <cstring>

// DEVPKEY_Gpu_Luid is not in every Windows SDK header set; define it ourselves.
// {60B193CB-5276-4D0F-96FC-F173ABAD3EC6}, 2  (Microsoft's documented PID for GPU LUID).
DEFINE_DEVPROPKEY(b70_DEVPKEY_Gpu_Luid,
    0x60b193cb, 0x5276, 0x4d0f, 0x96, 0xfc, 0xf1, 0x73, 0xab, 0xad, 0x3e, 0xc6, 2);

// PCIe link state (also frequently missing from older SDKs).
// GUID {3AB22E31-8264-4B4E-9AF5-A8D2D8E33E62}, PIDs 13..16.
DEFINE_DEVPROPKEY(b70_DEVPKEY_PciDevice_MaxLinkSpeed,
    0x3ab22e31, 0x8264, 0x4b4e, 0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62, 13);
DEFINE_DEVPROPKEY(b70_DEVPKEY_PciDevice_MaxLinkWidth,
    0x3ab22e31, 0x8264, 0x4b4e, 0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62, 14);
DEFINE_DEVPROPKEY(b70_DEVPKEY_PciDevice_CurrentLinkSpeed,
    0x3ab22e31, 0x8264, 0x4b4e, 0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62, 15);
DEFINE_DEVPROPKEY(b70_DEVPKEY_PciDevice_CurrentLinkWidth,
    0x3ab22e31, 0x8264, 0x4b4e, 0x9a, 0xf5, 0xa8, 0xd2, 0xd8, 0xe3, 0x3e, 0x62, 16);

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

namespace b70 {

namespace {

std::string wide_to_utf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool read_devprop_wstring(HDEVINFO h, SP_DEVINFO_DATA& d,
                          const DEVPROPKEY& key, std::wstring& out) {
    DEVPROPTYPE pt = 0;
    DWORD req = 0;
    SetupDiGetDevicePropertyW(h, &d, &key, &pt, nullptr, 0, &req, 0);
    if (req == 0) return false;
    out.assign(req / sizeof(wchar_t), L'\0');
    if (!SetupDiGetDevicePropertyW(h, &d, &key, &pt,
                                    reinterpret_cast<PBYTE>(out.data()), req, &req, 0)) {
        return false;
    }
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return true;
}

bool read_devprop_wstring_list(HDEVINFO h, SP_DEVINFO_DATA& d,
                               const DEVPROPKEY& key,
                               std::vector<std::wstring>& out) {
    DEVPROPTYPE pt = 0;
    DWORD req = 0;
    SetupDiGetDevicePropertyW(h, &d, &key, &pt, nullptr, 0, &req, 0);
    if (req == 0) return false;
    std::wstring blob(req / sizeof(wchar_t), L'\0');
    if (!SetupDiGetDevicePropertyW(h, &d, &key, &pt,
                                    reinterpret_cast<PBYTE>(blob.data()), req, &req, 0)) {
        return false;
    }
    const wchar_t* p = blob.c_str();
    while (*p) {
        std::wstring s(p);
        out.push_back(std::move(s));
        p += std::wcslen(p) + 1;
    }
    return true;
}

bool read_devprop_u32(HDEVINFO h, SP_DEVINFO_DATA& d,
                      const DEVPROPKEY& key, std::uint32_t& out) {
    DEVPROPTYPE pt = 0;
    DWORD req = 0;
    DWORD v = 0;
    if (!SetupDiGetDevicePropertyW(h, &d, &key, &pt,
                                    reinterpret_cast<PBYTE>(&v), sizeof(v), &req, 0)) {
        return false;
    }
    out = v;
    return true;
}

bool read_devprop_luid(HDEVINFO h, SP_DEVINFO_DATA& d,
                       const DEVPROPKEY& key, LUID& out) {
    DEVPROPTYPE pt = 0;
    DWORD req = 0;
    LUID v{};
    if (!SetupDiGetDevicePropertyW(h, &d, &key, &pt,
                                    reinterpret_cast<PBYTE>(&v), sizeof(v), &req, 0)) {
        return false;
    }
    out = v;
    return true;
}

bool read_instance_id(HDEVINFO h, SP_DEVINFO_DATA& d, std::string& out) {
    DWORD req = 0;
    SetupDiGetDeviceInstanceIdA(h, &d, nullptr, 0, &req);
    if (req == 0) return false;
    out.assign(req, '\0');
    if (!SetupDiGetDeviceInstanceIdA(h, &d, out.data(), req, &req)) return false;
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return true;
}

std::string parse_pci_bdf(std::uint32_t bus, std::uint32_t address_devfunc) {
    const std::uint32_t dev = (address_devfunc >> 16) & 0xFFFF;
    const std::uint32_t func = address_devfunc & 0xFFFF;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0000:%02x:%02x.%x", bus, dev, func);
    return buf;
}

}

SetupApiEnumResult enumerate_display_devnodes() {
    SetupApiEnumResult out;
    HDEVINFO h = SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT);
    if (h == INVALID_HANDLE_VALUE) {
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "SetupDiGetClassDevsW failed: %lu", GetLastError());
        out.error = tmp;
        return out;
    }

    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof(d);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); ++i) {
        SetupApiDevnodeRecord r;

        std::wstring loc;
        if (read_devprop_wstring(h, d, DEVPKEY_Device_LocationInfo, loc)) {
            r.location_info_w = loc;
            r.location_info_utf8 = wide_to_utf8(loc.c_str());
        }

        LUID lu{};
        if (read_devprop_luid(h, d, b70_DEVPKEY_Gpu_Luid, lu)) {
            r.luid_high = lu.HighPart;
            r.luid_low = lu.LowPart;
            r.luid_raw = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(lu.HighPart)) << 32)
                         | static_cast<std::uint64_t>(lu.LowPart);
            r.luid_present = true;
        }

        std::uint32_t bus = 0;
        std::uint32_t addr = 0;
        const bool got_bus = read_devprop_u32(h, d, DEVPKEY_Device_BusNumber, bus);
        const bool got_addr = read_devprop_u32(h, d, DEVPKEY_Device_Address, addr);
        if (got_bus && got_addr) {
            r.bus_number = bus;
            r.address_devfunc = addr;
            r.pci_bdf = parse_pci_bdf(bus, addr);
            r.bdf_present = true;
        }

        read_devprop_wstring_list(h, d, DEVPKEY_Device_LocationPaths, r.location_paths_w);
        read_instance_id(h, d, r.device_instance_id);

        std::uint32_t v = 0;
        if (read_devprop_u32(h, d, b70_DEVPKEY_PciDevice_MaxLinkSpeed, v))      r.max_link_speed     = v;
        if (read_devprop_u32(h, d, b70_DEVPKEY_PciDevice_MaxLinkWidth, v))      r.max_link_width     = v;
        if (read_devprop_u32(h, d, b70_DEVPKEY_PciDevice_CurrentLinkSpeed, v))  r.current_link_speed = v;
        if (read_devprop_u32(h, d, b70_DEVPKEY_PciDevice_CurrentLinkWidth, v))  r.current_link_width = v;

        out.devnodes.push_back(std::move(r));
    }
    SetupDiDestroyDeviceInfoList(h);
    return out;
}

}
