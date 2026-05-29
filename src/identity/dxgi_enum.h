#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace b70 {

struct DxgiAdapterRecord {
    std::uint32_t dxgi_index = 0;
    std::uint64_t luid_raw = 0;     // (HighPart << 32) | LowPart, treat as opaque 64-bit
    std::int32_t  luid_high = 0;
    std::uint32_t luid_low = 0;
    std::wstring  description_w;
    std::string   description_utf8;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t subsys_id = 0;
    std::uint32_t revision = 0;
    std::uint64_t dedicated_video_memory = 0;
    std::uint64_t dedicated_system_memory = 0;
    std::uint64_t shared_system_memory = 0;
    bool is_software = false;
    bool is_remote = false;
};

struct DxgiEnumResult {
    std::vector<DxgiAdapterRecord> adapters;
    std::string error;                          // empty if no error
    bool factory_created = false;
    std::string factory_iface;                  // "IDXGIFactory6" or "IDXGIFactory1"
};

DxgiEnumResult enumerate_dxgi_adapters();

}
