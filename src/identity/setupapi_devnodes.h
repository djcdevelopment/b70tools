#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

struct SetupApiDevnodeRecord {
    std::string  device_instance_id;            // "PCI\\VEN_8086&DEV_E20B&..."
    std::wstring location_info_w;               // "PCI bus N, device N, function N"
    std::string  location_info_utf8;
    std::int32_t luid_high = 0;
    std::uint32_t luid_low = 0;
    std::uint64_t luid_raw = 0;
    bool luid_present = false;
    std::uint32_t bus_number = 0;
    std::uint32_t address_devfunc = 0;          // device << 16 | function
    bool bdf_present = false;
    std::string  pci_bdf;                       // "0000:01:00.0" (best-effort)
    std::vector<std::wstring> location_paths_w;

    // PCIe link state (Windows DEVPKEY_PciDevice_*). 0 = not available.
    // Speed: 1=Gen1, 2=Gen2, 3=Gen3, 4=Gen4, 5=Gen5; Width: literal lane count.
    std::uint32_t max_link_speed = 0;
    std::uint32_t max_link_width = 0;
    std::uint32_t current_link_speed = 0;
    std::uint32_t current_link_width = 0;
};

struct SetupApiEnumResult {
    std::vector<SetupApiDevnodeRecord> devnodes;
    std::string error;
};

SetupApiEnumResult enumerate_display_devnodes();

}
