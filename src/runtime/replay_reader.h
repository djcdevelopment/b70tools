#pragma once

#include "bus/event_bus.h"

#include <cstdint>
#include <filesystem>

namespace b70 {

struct ReplayStats {
    std::uint64_t lines_total = 0;
    std::uint64_t lines_parsed = 0;
    std::uint64_t lines_failed = 0;
    std::uint64_t metric_samples = 0;
    std::uint64_t state_transitions = 0;
    std::uint64_t identities = 0;
    std::uint64_t disagreements = 0;
    std::uint64_t epoch_boundaries = 0;
    std::uint64_t fingerprints = 0;
    std::uint64_t audits = 0;
};

ReplayStats replay_jsonl(const std::filesystem::path& jsonl_path, EventBus& bus);

}
