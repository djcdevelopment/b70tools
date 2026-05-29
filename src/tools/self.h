#pragma once

#include <filesystem>

namespace b70 {

int run_self_command(const std::filesystem::path& run_dir_or_jsonl);

}
