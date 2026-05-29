#pragma once

#include "schema/events.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace b70 {

struct LibrarySnapshot {
    std::uint32_t thread_count = 0;
    std::vector<std::string> module_names;  // file names only (e.g. "vulkan-1.dll")
    std::uint64_t timestamp_qpc = 0;
    std::int64_t  rss_bytes = 0;
};

LibrarySnapshot take_library_snapshot();

CollectorAuditRecord build_audit_record(const std::string& collector_name,
                                        const LibrarySnapshot& before,
                                        const LibrarySnapshot& after,
                                        bool declared_truly_passive,
                                        bool declared_driver_passive,
                                        std::uint32_t session_epoch);

}
