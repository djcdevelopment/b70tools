#include "identity/dxgi_enum.h"

#include <windows.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

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

void fill_from_desc1(DxgiAdapterRecord& r, const DXGI_ADAPTER_DESC1& d) {
    r.description_w = d.Description;
    r.description_utf8 = wide_to_utf8(d.Description);
    r.vendor_id = d.VendorId;
    r.device_id = d.DeviceId;
    r.subsys_id = d.SubSysId;
    r.revision = d.Revision;
    r.dedicated_video_memory = d.DedicatedVideoMemory;
    r.dedicated_system_memory = d.DedicatedSystemMemory;
    r.shared_system_memory = d.SharedSystemMemory;
    r.luid_high = d.AdapterLuid.HighPart;
    r.luid_low = d.AdapterLuid.LowPart;
    r.luid_raw = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(d.AdapterLuid.HighPart)) << 32)
                 | static_cast<std::uint64_t>(d.AdapterLuid.LowPart);
    r.is_software = (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
    r.is_remote = (d.Flags & DXGI_ADAPTER_FLAG_REMOTE) != 0;
}

}

DxgiEnumResult enumerate_dxgi_adapters() {
    DxgiEnumResult out;

    IDXGIFactory6* fac6 = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&fac6));
    if (SUCCEEDED(hr) && fac6) {
        out.factory_created = true;
        out.factory_iface = "IDXGIFactory6";

        for (UINT i = 0; ; ++i) {
            IDXGIAdapter1* a1 = nullptr;
            HRESULT er = fac6->EnumAdapterByGpuPreference(
                i, DXGI_GPU_PREFERENCE_UNSPECIFIED, __uuidof(IDXGIAdapter1),
                reinterpret_cast<void**>(&a1));
            if (er == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(er) || !a1) continue;

            DXGI_ADAPTER_DESC1 d{};
            if (SUCCEEDED(a1->GetDesc1(&d))) {
                DxgiAdapterRecord r;
                r.dxgi_index = i;
                fill_from_desc1(r, d);
                out.adapters.push_back(std::move(r));
            }
            a1->Release();
        }
        fac6->Release();
        return out;
    }

    IDXGIFactory1* fac1 = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&fac1));
    if (FAILED(hr) || !fac1) {
        char tmp[128];
        std::snprintf(tmp, sizeof(tmp), "CreateDXGIFactory1 failed: HRESULT 0x%08lx",
                      static_cast<unsigned long>(hr));
        out.error = tmp;
        return out;
    }
    out.factory_created = true;
    out.factory_iface = "IDXGIFactory1";

    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* a1 = nullptr;
        HRESULT er = fac1->EnumAdapters1(i, &a1);
        if (er == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(er) || !a1) continue;

        DXGI_ADAPTER_DESC1 d{};
        if (SUCCEEDED(a1->GetDesc1(&d))) {
            DxgiAdapterRecord r;
            r.dxgi_index = i;
            fill_from_desc1(r, d);
            out.adapters.push_back(std::move(r));
        }
        a1->Release();
    }
    fac1->Release();
    return out;
}

}
