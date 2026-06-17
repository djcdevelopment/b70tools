#include "tools/adapters.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
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
           (line[p] == '-' || line[p] == '+' ||
            (line[p] >= '0' && line[p] <= '9') ||
            line[p] == '.' || line[p] == 'e' || line[p] == 'E')) {
        ++p;
    }
    std::int64_t v = 0;
    auto r = std::from_chars(line.data() + start, line.data() + p, v);
    if (r.ec == std::errc{}) return v;
    std::string tmp(line.data() + start, p - start);
    try { return static_cast<std::int64_t>(std::stod(tmp)); }
    catch (...) { return std::nullopt; }
}

std::optional<double> find_double(std::string_view line, std::string_view key) {
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
           (line[p] == '-' || line[p] == '+' ||
            (line[p] >= '0' && line[p] <= '9') ||
            line[p] == '.' || line[p] == 'e' || line[p] == 'E')) {
        ++p;
    }
    std::string tmp(line.data() + start, p - start);
    try { return std::stod(tmp); }
    catch (...) { return std::nullopt; }
}

std::filesystem::path resolve_jsonl_path(const std::filesystem::path& arg) {
    if (std::filesystem::is_directory(arg)) {
        auto p = arg / "events.jsonl";
        if (std::filesystem::exists(p)) return p;
    }
    return arg;
}

// Per-metric time-series accumulator. Tracks first/last (for rate derivation),
// min/max/mean (for value-range reporting), and a sample count.
struct SeriesAcc {
    double first = 0.0, last = 0.0;
    std::uint64_t first_ts = 0, last_ts = 0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    std::size_t n = 0;
    std::string unit;
    std::string source;
    std::map<std::string, std::size_t> source_counts;
    std::size_t low_confidence_samples = 0;
    std::size_t untrusted_samples = 0;
    std::size_t stale_samples = 0;
    std::size_t impossible_samples = 0;

    void add(double v, std::uint64_t ts, const std::string& u, const std::string& src,
             const std::string& confidence, const std::string& observation_kind,
             std::uint64_t flags, bool impossible) {
        if (n == 0) { first = v; first_ts = ts; unit = u; source = src; }
        last = v; last_ts = ts;
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
        ++source_counts[src];
        if (confidence == "Low" || confidence == "Disagreed") ++low_confidence_samples;
        if (observation_kind == "Reported_Untrusted") ++untrusted_samples;
        if ((flags & 1ull) != 0) ++stale_samples;
        if (impossible) ++impossible_samples;
        ++n;
    }
    double mean() const { return n ? sum / n : 0.0; }
    double rate() const {
        if (n < 2) return 0.0;
        if (last_ts <= first_ts) return 0.0;
        const double dt_s = (last_ts - first_ts) / 1e9;
        if (dt_s <= 0) return 0.0;
        return (last - first) / dt_s;
    }
};

struct AdapterAcc {
    std::string id;
    std::string description;
    std::string pci_bdf;
    std::string driver_uuid_hex;
    std::uint64_t luid = 0;
    std::uint64_t dedicated_video_memory = 0;
    std::uint64_t shared_system_memory = 0;
    std::vector<std::tuple<std::string, std::string, std::string>> state_transitions;  // (from, to, reason)
    std::map<std::string, SeriesAcc> series;
    std::map<std::string, std::size_t> disagree_class_counts;
};

std::string human_bytes(double b) {
    char buf[40];
    if (b >= (1ull << 30))      std::snprintf(buf, sizeof(buf), "%.2f GiB", b / static_cast<double>(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MiB", b / static_cast<double>(1ull << 20));
    else if (b >= (1ull << 10)) std::snprintf(buf, sizeof(buf), "%.1f KiB", b / static_cast<double>(1ull << 10));
    else                        std::snprintf(buf, sizeof(buf), "%.0f B",   b);
    return buf;
}

std::string human_hz(double hz) {
    char buf[40];
    if (hz >= 1e9)      std::snprintf(buf, sizeof(buf), "%.3f GHz", hz / 1e9);
    else if (hz >= 1e6) std::snprintf(buf, sizeof(buf), "%.1f MHz", hz / 1e6);
    else if (hz >= 1e3) std::snprintf(buf, sizeof(buf), "%.1f kHz", hz / 1e3);
    else                std::snprintf(buf, sizeof(buf), "%.0f Hz",  hz);
    return buf;
}

std::string human_value(double v, const std::string& unit) {
    if (unit == "Bytes")          return human_bytes(v);
    if (unit == "Hertz")          return human_hz(v);
    if (unit == "BytesPerSecond") return human_bytes(v) + "/s";
    if (unit == "Celsius")        { char b[24]; std::snprintf(b, sizeof(b), "%.1f C", v); return b; }
    if (unit == "Volts")          { char b[24]; std::snprintf(b, sizeof(b), "%.3f V", v); return b; }
    if (unit == "Percent")        { char b[24]; std::snprintf(b, sizeof(b), "%.1f %%", v); return b; }
    if (unit == "Rpm")            { char b[24]; std::snprintf(b, sizeof(b), "%.0f RPM", v); return b; }
    if (unit == "Nanoseconds")    {
        char b[32];
        if (v >= 1e9) std::snprintf(b, sizeof(b), "%.2f s",  v / 1e9);
        else if (v >= 1e6) std::snprintf(b, sizeof(b), "%.1f ms", v / 1e6);
        else std::snprintf(b, sizeof(b), "%.0f ns", v);
        return b;
    }
    char b[40]; std::snprintf(b, sizeof(b), "%.4g", v); return b;
}

bool is_activity_counter(const std::string& name) {
    return name.find(".activity.") != std::string::npos && name.find("_counter") != std::string::npos;
}

bool is_energy_counter(const std::string& name) {
    return name.find(".energy_j_counter") != std::string::npos;
}

bool is_vram_usage_metric(const std::string& name) {
    return name == "vram.local.current_usage_bytes" ||
           name == "vulkan.heap0.usage_bytes";
}

bool is_physically_impossible_sample(const std::string& name, const std::string& unit, double v) {
    if (unit == "Volts") return v < 0.0 || v > 2.0;
    if (unit == "Celsius") return v < -10.0 || v > 120.0;
    if (unit == "Percent") return v < -1.0 || v > 110.0;
    if (unit == "Rpm") return v < 0.0 || v > 10000.0;
    if (unit == "BytesPerSecond") {
        constexpr double kBwCeilingBps = 10.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;
        return v < 0.0 || v > kBwCeilingBps;
    }
    if (unit == "Hertz") {
        const bool is_vram = name.find("vram.") == 0;
        const bool is_freq = name.find("frequency") != std::string::npos;
        if (!is_freq) return false;
        const double ceiling = is_vram ? 4.0e9 : 5.0e9;
        return v < 0.0 || v > ceiling;
    }
    return false;
}

std::string source_summary(const SeriesAcc& s) {
    std::string out;
    for (const auto& [src, n] : s.source_counts) {
        if (!out.empty()) out += ",";
        out += src;
        if (s.source_counts.size() > 1) {
            out += ":";
            out += std::to_string(n);
        }
    }
    return out.empty() ? s.source : out;
}

// Whitelist of metrics worth keeping a time-series for (the rest get dropped to save memory).
bool is_track_metric(const std::string& n) {
    static const std::string_view names[] = {
        "vram.local.current_usage_bytes",
        "vram.local.budget_bytes",
        "vram.non_local.current_usage_bytes",
        "gpu.adapter.vram.local.bytes_committed",
        "gpu.adapter.vram.non_local.bytes_committed",
        "vulkan.heap0.usage_bytes",
        "vulkan.heap0.budget_bytes",
        "vulkan.heap1.usage_bytes",
        "gpu.frequency_hz",
        "gpu.frequency_max_hz",
        "gpu.voltage_v",
        "gpu.temperature_c",
        "gpu.activity.global_counter",
        "gpu.activity.render_compute_counter",
        "gpu.activity.media_counter",
        "gpu.energy_j_counter",
        "gpu.power_pct",
        "vram.frequency_hz",
        "vram.temperature_c",
        "card.energy_j_counter",
        "card.fan0.speed",
        "gpu.memory_frequency_hz",
        "gpu.memory_bandwidth_bps",
        "gpu.pcie_bandwidth_bps",
    };
    for (auto x : names) if (n == x) return true;
    return false;
}

}  // namespace

int run_adapters_command(const std::filesystem::path& arg) {
    namespace fs = std::filesystem;
    fs::path jsonl = resolve_jsonl_path(arg);
    if (!fs::exists(jsonl)) {
        std::fprintf(stderr, "adapters: no events.jsonl at %s\n", jsonl.string().c_str());
        return 2;
    }
    std::ifstream in(jsonl, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "adapters: cannot open %s\n", jsonl.string().c_str());
        return 2;
    }

    std::map<std::string, AdapterAcc> adapters;
    std::uint64_t first_metric_ts = 0;
    std::uint64_t last_metric_ts = 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string_view sv(line);
        auto kind = find_string(sv, "k");
        if (!kind) continue;

        if (*kind == "ai") {
            auto id = find_string(sv, "a").value_or("");
            auto& a = adapters[id];
            a.id = id;
            if (auto d = find_string(sv, "desc"); d) a.description = *d;
            if (auto b = find_string(sv, "bdf");  b) a.pci_bdf     = *b;
            if (auto u = find_string(sv, "druu"); u) a.driver_uuid_hex = *u;
            if (auto l = find_int(sv, "luid"); l) a.luid = static_cast<std::uint64_t>(*l);
            if (auto v = find_int(sv, "dvm"); v) a.dedicated_video_memory = static_cast<std::uint64_t>(*v);
            if (auto v = find_int(sv, "ssm"); v) a.shared_system_memory   = static_cast<std::uint64_t>(*v);

        } else if (*kind == "ast") {
            auto id = find_string(sv, "a").value_or("");
            auto& a = adapters[id];
            if (a.id.empty()) a.id = id;
            a.state_transitions.emplace_back(
                find_string(sv, "from").value_or(""),
                find_string(sv, "to").value_or(""),
                find_string(sv, "r").value_or(""));

        } else if (*kind == "ms") {
            auto name = find_string(sv, "n").value_or("");
            auto id   = find_string(sv, "a").value_or("");
            if (id.empty() || name.empty()) continue;
            if (!is_track_metric(name)) continue;
            auto v = find_double(sv, "v");
            if (!v) continue;
            auto& a = adapters[id];
            if (a.id.empty()) a.id = id;
            auto ts_i = find_int(sv, "t");
            const std::uint64_t ts = ts_i ? static_cast<std::uint64_t>(*ts_i) : 0;
            const std::string unit   = find_string(sv, "u").value_or("");
            const std::string source = find_string(sv, "s").value_or("");
            const std::string confidence = find_string(sv, "f").value_or("");
            const std::string observation_kind = find_string(sv, "o").value_or("");
            const std::uint64_t flags = static_cast<std::uint64_t>(find_int(sv, "g").value_or(0));
            a.series[name].add(*v, ts, unit, source, confidence, observation_kind, flags,
                               is_physically_impossible_sample(name, unit, *v));
            if (ts) {
                if (first_metric_ts == 0 || ts < first_metric_ts) first_metric_ts = ts;
                if (ts > last_metric_ts) last_metric_ts = ts;
            }

        } else if (*kind == "dr") {
            auto rule = find_string(sv, "rule").value_or("?");
            auto aid  = find_string(sv, "a").value_or("?");
            ++adapters[aid].disagree_class_counts[rule];
        }
    }

    const std::uint64_t run_span_ns =
        (last_metric_ts > first_metric_ts) ? (last_metric_ts - first_metric_ts) : 0;
    const double run_span_s = run_span_ns / 1e9;

    std::printf("# adapters detail\n");
    std::printf("file:           %s\n", jsonl.string().c_str());
    std::printf("run span:       %.2f s (first→last metric)\n", run_span_s);
    std::printf("adapters seen:  %zu\n", adapters.size());

    for (const auto& [id, a] : adapters) {
        std::printf("\n========================================================================\n");
        std::printf("%s — %s\n", id.c_str(), a.description.empty() ? "<unknown>" : a.description.c_str());
        std::printf("------------------------------------------------------------------------\n");
        std::printf("  LUID:           0x%016llx\n", static_cast<unsigned long long>(a.luid));
        std::printf("  PCI BDF:        %s\n", a.pci_bdf.empty() ? "<unknown>" : a.pci_bdf.c_str());
        std::printf("  Dedicated VRAM: %s\n", human_bytes(static_cast<double>(a.dedicated_video_memory)).c_str());
        std::printf("  Shared System:  %s\n", human_bytes(static_cast<double>(a.shared_system_memory)).c_str());

        // State trajectory
        if (!a.state_transitions.empty()) {
            std::printf("\n  state trajectory (%zu transitions):\n", a.state_transitions.size());
            for (const auto& [from, to, reason] : a.state_transitions) {
                std::printf("    %s → %s  [%s]\n", from.c_str(), to.c_str(), reason.c_str());
            }
        } else {
            std::printf("\n  state trajectory: <none recorded>\n");
        }

        // Activity rates — the headline question for inference experiments
        std::printf("\n  activity (IGCL counters, derived as Δcounter / Δwall):\n");
        bool any_activity = false;
        bool any_unusable_activity = false;
        for (const auto& key : {"gpu.activity.global_counter",
                                "gpu.activity.render_compute_counter",
                                "gpu.activity.media_counter"}) {
            auto it = a.series.find(key);
            if (it == a.series.end() || it->second.n < 2) continue;
            const auto& s = it->second;
            if (s.last_ts <= s.first_ts) continue;
            const double dt_s = (s.last_ts - s.first_ts) / 1e9;
            if (dt_s < 0.5) continue;  // run too short for meaningful rate
            const double delta = s.last - s.first;
            const double rate = delta / dt_s;
            const bool ns_unit = (s.unit == "Nanoseconds");
            const double activity_ratio = ns_unit ? (rate / 1e9) : 0.0;
            std::printf("    %-44s Δ %s over %.1f s → ",
                        key, human_value(delta, s.unit).c_str(), dt_s);
            if (ns_unit) {
                std::printf("%.1f%% activity", activity_ratio * 100.0);
                if (delta < 0) {
                    std::printf("  [UNUSABLE: counter regressed; prefer slower non-IGCL signals]");
                    any_unusable_activity = true;
                } else if (activity_ratio > 1) {
                    std::printf("  [UNUSABLE: counter advances %.0fx faster than wall clock; prefer slower non-IGCL signals]",
                                activity_ratio);
                    any_unusable_activity = true;
                }
                std::printf("\n");
            } else {
                std::printf("%.4g/s\n", rate);
            }
            any_activity = true;
        }
        if (!any_activity) std::printf("    (no IGCL activity counters or run too short for rate)\n");
        if (any_unusable_activity) {
            std::printf("    verdict: IGCL activity is source-degraded for this adapter; use PDH adapter memory, DXGI/Vulkan budget, D3DKMT thermals, and workload timestamps instead.\n");
        }

        // Memory residency — first vs last, peak
        std::printf("\n  memory / workload evidence (trust order: PDH adapter memory > DXGI/Vulkan process budgets > IGCL):\n");
        bool any_mem = false;
        for (const auto& key : {"gpu.adapter.vram.local.bytes_committed",
                                "gpu.adapter.vram.non_local.bytes_committed",
                                "vram.local.current_usage_bytes",
                                "vram.non_local.current_usage_bytes",
                                "vulkan.heap0.usage_bytes",
                                "vulkan.heap1.usage_bytes",
                                "vram.local.budget_bytes",
                                "vulkan.heap0.budget_bytes"}) {
            auto it = a.series.find(key);
            if (it == a.series.end() || it->second.n == 0) continue;
            const auto& s = it->second;
            std::printf("    %-44s first=%s  peak=%s  last=%s  (n=%zu, via %s)\n",
                        key,
                        human_value(s.first, s.unit).c_str(),
                        human_value(s.max,   s.unit).c_str(),
                        human_value(s.last,  s.unit).c_str(),
                        s.n,
                        source_summary(s).c_str());
            if (std::string(key).find("gpu.adapter.vram.") == 0) {
                std::printf("      ^ system-wide adapter memory; usable as slow fallback when IGCL counters are broken\n");
            }
            any_mem = true;
        }
        if (!any_mem) std::printf("    (no memory metrics in this run)\n");

        // Thermal / frequency / voltage envelope
        std::printf("\n  thermals / clocks / voltage envelope:\n");
        bool any_phys = false;
        for (const auto& key : {"gpu.frequency_hz", "vram.frequency_hz",
                                "gpu.voltage_v", "gpu.temperature_c", "vram.temperature_c",
                                "card.fan0.speed", "gpu.power_pct"}) {
            auto it = a.series.find(key);
            if (it == a.series.end() || it->second.n == 0) continue;
            const auto& s = it->second;
            std::printf("    %-30s min=%s  max=%s  mean=%s  (n=%zu, via %s)\n",
                        key,
                        human_value(s.min,  s.unit).c_str(),
                        human_value(s.max,  s.unit).c_str(),
                        human_value(s.mean(), s.unit).c_str(),
                        s.n,
                        source_summary(s).c_str());
            if (s.impossible_samples || s.untrusted_samples || s.low_confidence_samples) {
                std::printf("      ^ degraded samples: impossible=%zu untrusted=%zu low_confidence=%zu; do not use as authoritative\n",
                            s.impossible_samples, s.untrusted_samples, s.low_confidence_samples);
            }
            any_phys = true;
        }
        if (!any_phys) std::printf("    (no thermal/clock/voltage metrics in this run)\n");

        // Disagreements involving this adapter
        std::printf("\n  disagreements involving this adapter:\n");
        if (a.disagree_class_counts.empty()) {
            std::printf("    (none)\n");
        } else {
            for (const auto& [rule, n] : a.disagree_class_counts) {
                std::printf("    [%zu] %s\n", n, rule.c_str());
            }
        }
    }

    return 0;
}

}
