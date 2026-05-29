#include "runtime/library_audit.h"

#include "runtime/session.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_set>

#pragma comment(lib, "psapi.lib")

namespace b70 {

namespace {

std::uint32_t count_threads_in_process(DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    std::uint32_t n = 0;
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) ++n;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return n;
}

std::vector<std::string> list_modules() {
    std::vector<std::string> out;
    HMODULE mods[1024];
    DWORD needed = 0;
    HANDLE me = GetCurrentProcess();
    if (!EnumProcessModulesEx(me, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        return out;
    }
    const DWORD count = needed / sizeof(HMODULE);
    out.reserve(count);
    char name[MAX_PATH];
    for (DWORD i = 0; i < count; ++i) {
        DWORD n = GetModuleFileNameExA(me, mods[i], name, MAX_PATH);
        if (n == 0) continue;
        const char* slash = std::strrchr(name, '\\');
        const char* fwd   = std::strrchr(name, '/');
        const char* base = slash ? slash + 1 : (fwd ? fwd + 1 : name);
        out.emplace_back(base);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::int64_t current_rss_bytes() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                              sizeof(pmc))) {
        return static_cast<std::int64_t>(pmc.WorkingSetSize);
    }
    return 0;
}

}

LibrarySnapshot take_library_snapshot() {
    LibrarySnapshot s;
    s.thread_count = count_threads_in_process(GetCurrentProcessId());
    s.module_names = list_modules();
    s.timestamp_qpc = Session::now_qpc_ns();
    s.rss_bytes = current_rss_bytes();
    return s;
}

CollectorAuditRecord build_audit_record(const std::string& collector_name,
                                        const LibrarySnapshot& before,
                                        const LibrarySnapshot& after,
                                        bool declared_truly_passive,
                                        bool declared_driver_passive,
                                        std::uint32_t session_epoch) {
    CollectorAuditRecord rec;
    rec.collector_name = collector_name;
    rec.threads_before = before.thread_count;
    rec.threads_after  = after.thread_count;
    rec.init_wall_ns   = (after.timestamp_qpc >= before.timestamp_qpc)
                       ? (after.timestamp_qpc - before.timestamp_qpc) : 0;
    rec.rss_delta_bytes = after.rss_bytes - before.rss_bytes;
    rec.declared_truly_passive = declared_truly_passive;
    rec.declared_driver_passive = declared_driver_passive;
    rec.session_epoch = session_epoch;
    rec.timestamp_qpc = after.timestamp_qpc;

    std::unordered_set<std::string> seen(before.module_names.begin(), before.module_names.end());
    for (const auto& m : after.module_names) {
        if (!seen.count(m)) rec.modules_loaded.push_back(m);
    }

    const std::uint32_t added_threads =
        (after.thread_count > before.thread_count)
        ? (after.thread_count - before.thread_count) : 0;
    const std::size_t added_modules = rec.modules_loaded.size();

    // Third-party Vulkan layer detection — environment-fingerprint risk per plan §A.11.
    // Layer DLLs almost universally have "VkLayer" in their name (RTSSVkLayer64.dll,
    // VkLayer_obs.dll, VkLayer_steam_fossilize.dll, etc.). Intel's own ICD modules
    // (igvk64.dll) don't match this pattern.
    std::vector<std::string> third_party_layers;
    for (const auto& m : rec.modules_loaded) {
        if (m.find("VkLayer") != std::string::npos) third_party_layers.push_back(m);
    }

    if (declared_truly_passive && (added_threads > 0 || added_modules > 0)) {
        rec.observed_undeclared_side_effects = true;
        char tmp[224];
        std::snprintf(tmp, sizeof(tmp),
                      "collector declared TrulyPassive but audit observed +%u thread(s), +%zu module(s); "
                      "reclassify to DriverPassive or investigate",
                      added_threads, added_modules);
        rec.notes = tmp;
    } else if (declared_driver_passive && added_threads > 0) {
        char tmp[160];
        std::snprintf(tmp, sizeof(tmp),
                      "DriverPassive collector spawned %u thread(s) during init — investigate",
                      added_threads);
        rec.notes = tmp;
        rec.observed_undeclared_side_effects = true;
    } else if (added_modules > 0) {
        char tmp[160];
        std::snprintf(tmp, sizeof(tmp), "+%zu module(s) loaded during init (DriverPassive)", added_modules);
        rec.notes = tmp;
    }

    if (!third_party_layers.empty()) {
        std::string warn = " | third-party Vulkan layer(s) detected: ";
        for (std::size_t i = 0; i < third_party_layers.size(); ++i) {
            if (i) warn += ", ";
            warn += third_party_layers[i];
        }
        warn += " — fingerprint risk (may affect enumeration / perf / telemetry / reproducibility)";
        rec.notes += warn;
    }
    return rec;
}

}
