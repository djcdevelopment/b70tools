#include "tools/self.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b70 {

namespace {

std::string unescape(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            char e = raw[i + 1];
            switch (e) {
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case '"':  out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default:   out.push_back(e); break;
            }
            ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::optional<std::string> find_string(std::string_view line, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 4);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":\"");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    std::size_t q = p;
    while (q < line.size()) {
        if (line[q] == '\\' && q + 1 < line.size()) { q += 2; continue; }
        if (line[q] == '"') break;
        ++q;
    }
    return unescape(line.substr(p, q - p));
}

std::optional<std::int64_t> find_int(std::string_view line, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 3);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    if (p < line.size() && line[p] == '"') return std::nullopt;
    auto start = p;
    while (p < line.size() &&
           (line[p] == '-' || (line[p] >= '0' && line[p] <= '9'))) {
        ++p;
    }
    std::int64_t v = 0;
    auto r = std::from_chars(line.data() + start, line.data() + p, v);
    if (r.ec == std::errc{}) return v;
    return std::nullopt;
}

std::optional<bool> find_bool(std::string_view line, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 3);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    if (line.compare(p, 4, "true")  == 0) return true;
    if (line.compare(p, 5, "false") == 0) return false;
    return std::nullopt;
}

std::vector<std::string> find_string_array(std::string_view line, std::string_view key) {
    std::vector<std::string> out;
    std::string needle;
    needle.reserve(key.size() + 3);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":[");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return out;
    p += needle.size();
    while (p < line.size() && line[p] != ']') {
        while (p < line.size() && line[p] != '"' && line[p] != ']') ++p;
        if (p >= line.size() || line[p] == ']') break;
        ++p;
        std::size_t q = p;
        while (q < line.size()) {
            if (line[q] == '\\' && q + 1 < line.size()) { q += 2; continue; }
            if (line[q] == '"') break;
            ++q;
        }
        out.push_back(unescape(line.substr(p, q - p)));
        p = q + 1;
    }
    return out;
}

std::filesystem::path resolve_jsonl_path(const std::filesystem::path& arg) {
    if (std::filesystem::is_directory(arg)) {
        auto p = arg / "events.jsonl";
        if (std::filesystem::exists(p)) return p;
    }
    return arg;
}

struct CollectorAcc {
    std::string name;
    bool tp = false, dp = false, undec = false;
    std::int64_t rss = 0;
    std::uint64_t init_wall = 0;
    std::vector<std::string> modules;
    std::vector<std::string> third_party_layers;
    std::string note;
};

std::string human_ns(std::uint64_t ns) {
    char buf[32];
    if (ns >= 1'000'000'000ull) std::snprintf(buf, sizeof(buf), "%.2f s",  ns / 1e9);
    else if (ns >= 1'000'000)   std::snprintf(buf, sizeof(buf), "%.1f ms", ns / 1e6);
    else if (ns >= 1'000)       std::snprintf(buf, sizeof(buf), "%.1f us", ns / 1e3);
    else                        std::snprintf(buf, sizeof(buf), "%llu ns", static_cast<unsigned long long>(ns));
    return buf;
}

std::string human_bytes(std::int64_t b) {
    const bool neg = b < 0;
    const std::uint64_t mag = static_cast<std::uint64_t>(neg ? -b : b);
    char buf[40];
    if (mag >= (1ull << 30))      std::snprintf(buf, sizeof(buf), "%.2f GiB", mag / static_cast<double>(1ull << 30));
    else if (mag >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MiB", mag / static_cast<double>(1ull << 20));
    else if (mag >= (1ull << 10)) std::snprintf(buf, sizeof(buf), "%.1f KiB", mag / static_cast<double>(1ull << 10));
    else                          std::snprintf(buf, sizeof(buf), "%llu B",   static_cast<unsigned long long>(mag));
    return std::string(neg ? "-" : "") + buf;
}

}  // namespace

int run_self_command(const std::filesystem::path& arg) {
    namespace fs = std::filesystem;
    fs::path jsonl = resolve_jsonl_path(arg);
    if (!fs::exists(jsonl)) {
        std::fprintf(stderr, "self: no events.jsonl at %s\n", jsonl.string().c_str());
        return 2;
    }
    std::ifstream in(jsonl, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "self: cannot open %s\n", jsonl.string().c_str());
        return 2;
    }

    std::map<std::string, CollectorAcc> collectors;
    std::map<std::string, std::size_t> disagree_classes;
    std::size_t total_lines = 0;
    std::size_t metric_lines = 0;
    std::uint64_t first_ts = 0, last_ts = 0;

    std::string line;
    while (std::getline(in, line)) {
        ++total_lines;
        if (line.empty()) continue;
        std::string_view sv(line);
        auto kind = find_string(sv, "k");
        if (!kind) continue;

        if (auto t = find_int(sv, "t"); t && *t > 0) {
            if (first_ts == 0 || static_cast<std::uint64_t>(*t) < first_ts)
                first_ts = static_cast<std::uint64_t>(*t);
            if (static_cast<std::uint64_t>(*t) > last_ts)
                last_ts = static_cast<std::uint64_t>(*t);
        }

        if (*kind == "ms") {
            ++metric_lines;
        } else if (*kind == "car") {
            CollectorAcc c;
            c.name      = find_string(sv, "name").value_or("");
            c.tp        = find_bool(sv, "tp").value_or(false);
            c.dp        = find_bool(sv, "dp").value_or(false);
            c.undec     = find_bool(sv, "undec").value_or(false);
            c.rss       = find_int(sv, "rss").value_or(0);
            c.init_wall = static_cast<std::uint64_t>(find_int(sv, "iw").value_or(0));
            c.modules   = find_string_array(sv, "mods");
            c.note      = find_string(sv, "note").value_or("");
            for (const auto& m : c.modules) {
                if (m.find("VkLayer") != std::string::npos) c.third_party_layers.push_back(m);
            }
            auto it = collectors.find(c.name);
            if (it == collectors.end()) {
                collectors[c.name] = std::move(c);
            } else if (c.init_wall > 0 || !c.modules.empty()) {
                collectors[c.name] = std::move(c);
            }
        } else if (*kind == "dr") {
            auto rule = find_string(sv, "rule").value_or("?");
            ++disagree_classes[rule];
        }
    }

    std::error_code ec;
    const auto file_size = fs::file_size(jsonl, ec);
    const double duration_s = (last_ts > first_ts) ? (last_ts - first_ts) / 1e9 : 0.0;

    std::printf("# b70tools self — observation cost\n");
    std::printf("file:     %s\n", jsonl.string().c_str());
    std::printf("duration: %.2f s (first→last metric)\n", duration_s);
    std::printf("lines:    %zu (metric: %zu)\n", total_lines, metric_lines);

    std::int64_t rss_total = 0;
    std::uint64_t init_total = 0;
    for (const auto& [_, c] : collectors) { rss_total += c.rss; init_total += c.init_wall; }

    std::printf("\n## Per-collector cost ledger\n");
    if (collectors.empty()) {
        std::printf("  (no audit records)\n");
    } else {
        std::printf("  %-30s  %-15s  %-8s  %-10s  %-6s  %s\n",
                    "collector", "declared", "init", "rss Δ", "mods+", "notes");
        std::printf("  %-30s  %-15s  %-8s  %-10s  %-6s  %s\n",
                    "----------", "--------", "----", "-----", "-----", "-----");
        std::vector<const CollectorAcc*> sorted;
        for (const auto& [_, c] : collectors) sorted.push_back(&c);
        std::sort(sorted.begin(), sorted.end(),
                  [](const CollectorAcc* a, const CollectorAcc* b) { return a->name < b->name; });
        for (const auto* c : sorted) {
            const char* decl = c->tp ? "TrulyPassive"
                              : c->dp ? "DriverPassive" : "?";
            std::printf("  %-30s  %-15s  %-8s  %-10s  %-6zu  %s%s\n",
                        c->name.c_str(),
                        decl,
                        human_ns(c->init_wall).c_str(),
                        human_bytes(c->rss).c_str(),
                        c->modules.size(),
                        c->undec ? "[UNDECLARED] " : "",
                        c->note.c_str());
            if (!c->third_party_layers.empty()) {
                std::printf("      third-party layers:");
                for (const auto& m : c->third_party_layers) std::printf(" %s", m.c_str());
                std::printf("\n");
            }
        }
    }

    std::printf("\n## Totals\n");
    std::printf("  RSS attributed to collector inits:  %s\n",  human_bytes(rss_total).c_str());
    std::printf("  init wall time total:               %s\n",  human_ns(init_total).c_str());
    std::printf("  jsonl on disk:                      %lld bytes",
                static_cast<long long>(file_size));
    if (duration_s > 0) {
        std::printf("  (%.1f B/s, %.1f KiB/min)\n",
                    file_size / duration_s,
                    (file_size / duration_s) * 60.0 / 1024.0);
    } else {
        std::printf("\n");
    }
    if (total_lines > 0)
        std::printf("  bytes per event:                    %.1f\n",
                    static_cast<double>(file_size) / static_cast<double>(total_lines));

    std::printf("\n## do-no-harm budget check (per plan §A.2)\n");
    constexpr std::int64_t kRssBudget = 50ll * 1024 * 1024;
    constexpr std::int64_t kRssStretch = 30ll * 1024 * 1024;
    const std::int64_t rss_obs = (rss_total > 0) ? rss_total : 0;
    std::printf("  observed collector-attributed RSS:  %s\n", human_bytes(rss_obs).c_str());
    std::printf("  default budget (< 50 MiB):          %s\n",
                rss_obs < kRssBudget ? "PASS" : "EXCEEDED");
    std::printf("  stretch budget (< 30 MiB):          %s\n",
                rss_obs < kRssStretch ? "PASS" : "above stretch (acceptable)");
    std::printf("  note: actual process RSS may exceed this — the per-collector audit only\n");
    std::printf("        captures RSS deltas during init, not background drift.\n");

    std::printf("\n## Disagreement noise floor\n");
    if (disagree_classes.empty()) {
        std::printf("  (none)\n");
    } else {
        for (const auto& [r, n] : disagree_classes) {
            std::printf("  [%zu] %s\n", n, r.c_str());
        }
        if (duration_s > 0) {
            std::size_t total = 0;
            for (const auto& [_, n] : disagree_classes) total += n;
            std::printf("  rate: %.2f reports / minute\n", (total / duration_s) * 60.0);
        }
    }

    return 0;
}

}
