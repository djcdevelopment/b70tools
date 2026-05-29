#include "tools/summarize.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b70 {

namespace {

// --- minimal JSON field extraction (b70tools-compact-format-aware) -----------
// These are NOT a full JSON parser. They assume the b70tools writer's output,
// which we control: keys are unique within a line, strings are simply quoted
// with backslash escapes, numbers are vanilla decimals or scientific notation.

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
    if (p < line.size() && line[p] == '"') return std::nullopt;  // not an int
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
    // Fallback for scientific or fractional: parse double then cast.
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

// Parse `"key":[ "s1", "s2", ... ]` into a vector of strings.
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

// --- accumulators -----------------------------------------------------------

struct PeakMetric {
    double value = 0.0;
    std::string unit;
    std::string source;
    bool initialized = false;
};

struct AdapterAcc {
    std::string id;
    std::string description;
    std::string pci_bdf;
    std::string driver_uuid_hex;
    std::string driver_uuid_decoded;
    std::uint64_t luid = 0;
    std::uint64_t dedicated_video_memory = 0;
    std::uint64_t shared_system_memory = 0;
    std::string highest_state;       // "Unknown" | "Idle" | "Awake" | "ActiveCompute" | ...
    std::size_t state_transitions = 0;
    std::map<std::string, PeakMetric> peaks;
};

struct CollectorAcc {
    std::string name;
    bool declared_truly_passive = false;
    bool declared_driver_passive = false;
    bool observed_undeclared = false;
    std::uint32_t threads_before = 0;
    std::uint32_t threads_after = 0;
    std::uint64_t init_wall_ns = 0;
    std::int64_t  rss_delta_bytes = 0;
    std::vector<std::string> modules_loaded;
    std::vector<std::string> third_party_layers;
    std::string note;
    std::size_t binding_records = 0;
};

struct DisagreementAcc {
    std::string rule;
    std::string adapter_id;
    std::string explanation;
    std::vector<std::string> sources;
    std::uint64_t timestamp_qpc = 0;
};

struct FingerprintAcc {
    bool present = false;
    std::string windows_build;
    std::string bios_version;
    std::string bios_date;
    std::string intel_driver_version;
    std::string vulkan_runtime;
    bool hags = false;
    bool wdac = false;
    bool a4g = false;
    std::vector<std::string> per_adapter_notes;
};

struct EventCounts {
    std::size_t total = 0;
    std::size_t metric_samples = 0;
    std::size_t adapter_identities = 0;
    std::size_t adapter_state_transitions = 0;
    std::size_t disagreement_reports = 0;
    std::size_t epoch_boundaries = 0;
    std::size_t fingerprints = 0;
    std::size_t collector_audits = 0;
};

int state_rank(std::string_view s) {
    // Used to compute "highest state reached" — higher rank means "more awake."
    if (s == "Unknown")                 return 0;
    if (s == "Lost")                    return 0;
    if (s == "Reenumerating")           return 1;
    if (s == "Idle")                    return 2;
    if (s == "Awake")                   return 3;
    if (s == "ActiveCompute")           return 4;
    if (s == "SuspectedComputeHidden")  return 3;
    if (s == "PostTDR")                 return 1;
    return 0;
}

bool is_peak_metric_name(std::string_view n) {
    // Curated list — metrics worth peak-tracking for a one-shot summary.
    static const std::string_view names[] = {
        "vram.local.current_usage_bytes",
        "vram.local.budget_bytes",
        "vram.non_local.current_usage_bytes",
        "vulkan.heap0.usage_bytes",
        "vulkan.heap1.usage_bytes",
        "vulkan.heap0.budget_bytes",
        "gpu.temperature_c",
        "gpu.frequency_hz",
        "gpu.voltage_v",
        "gpu.activity.global_counter",
        "gpu.activity.render_compute_counter",
        "gpu.activity.media_counter",
        "vram.frequency_hz",
        "card.energy_j_counter",
    };
    for (auto x : names) if (n == x) return true;
    return false;
}

// --- summarizer body --------------------------------------------------------

std::string human_bytes(std::uint64_t b) {
    char buf[32];
    if (b >= (1ull << 30))      std::snprintf(buf, sizeof(buf), "%.2f GiB", b / static_cast<double>(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MiB", b / static_cast<double>(1ull << 20));
    else if (b >= (1ull << 10)) std::snprintf(buf, sizeof(buf), "%.1f KiB", b / static_cast<double>(1ull << 10));
    else                        std::snprintf(buf, sizeof(buf), "%llu B",   static_cast<unsigned long long>(b));
    return buf;
}

std::string human_hz(double hz) {
    char buf[32];
    if (hz >= 1e9)      std::snprintf(buf, sizeof(buf), "%.3f GHz", hz / 1e9);
    else if (hz >= 1e6) std::snprintf(buf, sizeof(buf), "%.1f MHz", hz / 1e6);
    else if (hz >= 1e3) std::snprintf(buf, sizeof(buf), "%.1f kHz", hz / 1e3);
    else                std::snprintf(buf, sizeof(buf), "%.0f Hz",  hz);
    return buf;
}

std::string human_ns(std::uint64_t ns) {
    char buf[32];
    if (ns >= 1'000'000'000ull) std::snprintf(buf, sizeof(buf), "%.2f s",  ns / 1e9);
    else if (ns >= 1'000'000)   std::snprintf(buf, sizeof(buf), "%.1f ms", ns / 1e6);
    else if (ns >= 1'000)       std::snprintf(buf, sizeof(buf), "%.1f us", ns / 1e3);
    else                        std::snprintf(buf, sizeof(buf), "%llu ns", static_cast<unsigned long long>(ns));
    return buf;
}

std::string human_rss_delta(std::int64_t b) {
    const std::uint64_t mag = static_cast<std::uint64_t>(b < 0 ? -b : b);
    const std::string s = human_bytes(mag);
    return std::string(b < 0 ? "-" : "+") + s;
}

std::string decode_intel_driver_uuid_ascii(const std::string& uuid_hex) {
    std::string raw;
    for (char c : uuid_hex) { if (c != '-') raw.push_back(c); }
    if (raw.size() != 32) return {};
    std::string out;
    for (std::size_t i = 0; i < raw.size(); i += 2) {
        unsigned v = 0;
        if (std::sscanf(raw.c_str() + i, "%2x", &v) != 1) return {};
        if (v == 0) break;
        if (v < 0x20 || v > 0x7e) return {};
        out.push_back(static_cast<char>(v));
    }
    return out;
}

void update_peak(AdapterAcc& a, const std::string& name, double v,
                 const std::string& unit, const std::string& source) {
    auto it = a.peaks.find(name);
    if (it == a.peaks.end() || v > it->second.value || !it->second.initialized) {
        PeakMetric p;
        p.value = v;
        p.unit = unit;
        p.source = source;
        p.initialized = true;
        a.peaks[name] = p;
    }
}

std::filesystem::path resolve_jsonl_path(const std::filesystem::path& arg) {
    if (std::filesystem::is_directory(arg)) {
        auto p = arg / "events.jsonl";
        if (std::filesystem::exists(p)) return p;
    }
    return arg;
}

}  // namespace

int run_summarize_command(const std::filesystem::path& arg) {
    namespace fs = std::filesystem;
    fs::path jsonl = resolve_jsonl_path(arg);
    if (!fs::exists(jsonl)) {
        std::fprintf(stderr, "summarize: no events.jsonl at %s\n", jsonl.string().c_str());
        return 2;
    }

    std::ifstream in(jsonl, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "summarize: cannot open %s\n", jsonl.string().c_str());
        return 2;
    }

    std::map<std::string, AdapterAcc> adapters;
    std::vector<CollectorAcc> collector_audits;
    std::vector<DisagreementAcc> disagreements;
    FingerprintAcc fp;
    EventCounts counts;
    std::uint32_t max_epoch = 0;
    std::uint64_t first_ts = 0;
    std::uint64_t last_ts  = 0;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        ++counts.total;
        std::string_view sv(line);

        auto kind = find_string(sv, "k");
        if (!kind) continue;

        if (auto t = find_int(sv, "t"); t && *t > 0) {
            if (first_ts == 0) first_ts = static_cast<std::uint64_t>(*t);
            last_ts = static_cast<std::uint64_t>(*t);
        }
        if (auto e = find_int(sv, "e"); e) {
            max_epoch = std::max(max_epoch, static_cast<std::uint32_t>(*e));
        }

        if (*kind == "ai") {
            ++counts.adapter_identities;
            auto id = find_string(sv, "a").value_or("");
            auto& a = adapters[id];
            a.id = id;
            if (auto d = find_string(sv, "desc"); d) a.description = *d;
            if (auto b = find_string(sv, "bdf");  b) a.pci_bdf     = *b;
            if (auto u = find_string(sv, "druu"); u) {
                a.driver_uuid_hex = *u;
                a.driver_uuid_decoded = decode_intel_driver_uuid_ascii(*u);
            }
            if (auto l = find_int(sv, "luid"); l) a.luid = static_cast<std::uint64_t>(*l);
            if (auto v = find_int(sv, "dvm"); v) a.dedicated_video_memory = static_cast<std::uint64_t>(*v);
            if (auto v = find_int(sv, "ssm"); v) a.shared_system_memory   = static_cast<std::uint64_t>(*v);

        } else if (*kind == "ast") {
            ++counts.adapter_state_transitions;
            auto id = find_string(sv, "a").value_or("");
            auto& a = adapters[id];
            if (a.id.empty()) a.id = id;
            auto to = find_string(sv, "to").value_or("Unknown");
            if (state_rank(to) > state_rank(a.highest_state)) a.highest_state = to;
            ++a.state_transitions;

        } else if (*kind == "ms") {
            ++counts.metric_samples;
            auto name   = find_string(sv, "n").value_or("");
            auto id     = find_string(sv, "a").value_or("");
            if (id.empty() || name.empty()) continue;
            if (!is_peak_metric_name(name)) continue;
            auto v = find_double(sv, "v");
            if (!v) continue;
            auto& a = adapters[id];
            if (a.id.empty()) a.id = id;
            auto unit   = find_string(sv, "u").value_or("");
            auto source = find_string(sv, "s").value_or("");
            update_peak(a, name, *v, unit, source);

        } else if (*kind == "dr") {
            ++counts.disagreement_reports;
            DisagreementAcc d;
            if (auto r = find_string(sv, "rule"); r) d.rule = *r;
            d.adapter_id  = find_string(sv, "a").value_or("");
            d.explanation = find_string(sv, "expl").value_or("");
            d.sources     = find_string_array(sv, "srcs");
            if (auto t = find_int(sv, "t"); t) d.timestamp_qpc = static_cast<std::uint64_t>(*t);
            disagreements.push_back(std::move(d));

        } else if (*kind == "seb") {
            ++counts.epoch_boundaries;

        } else if (*kind == "drf") {
            ++counts.fingerprints;
            fp.present = true;
            fp.windows_build       = find_string(sv, "win").value_or("");
            fp.bios_version        = find_string(sv, "bios").value_or("");
            fp.bios_date           = find_string(sv, "biosd").value_or("");
            fp.intel_driver_version = find_string(sv, "idrv").value_or("");
            fp.vulkan_runtime      = find_string(sv, "vkrt").value_or("");
            fp.hags = find_bool(sv, "hags").value_or(false);
            fp.wdac = find_bool(sv, "wdac").value_or(false);
            fp.a4g  = find_bool(sv, "a4g").value_or(false);
            fp.per_adapter_notes = find_string_array(sv, "notes");

        } else if (*kind == "car") {
            ++counts.collector_audits;
            CollectorAcc c;
            c.name                    = find_string(sv, "name").value_or("");
            c.declared_truly_passive  = find_bool(sv, "tp").value_or(false);
            c.declared_driver_passive = find_bool(sv, "dp").value_or(false);
            c.observed_undeclared     = find_bool(sv, "undec").value_or(false);
            c.threads_before          = static_cast<std::uint32_t>(find_int(sv, "tb").value_or(0));
            c.threads_after           = static_cast<std::uint32_t>(find_int(sv, "ta").value_or(0));
            c.init_wall_ns            = static_cast<std::uint64_t>(find_int(sv, "iw").value_or(0));
            c.rss_delta_bytes         = find_int(sv, "rss").value_or(0);
            c.modules_loaded          = find_string_array(sv, "mods");
            c.note                    = find_string(sv, "note").value_or("");
            for (const auto& m : c.modules_loaded) {
                if (m.find("VkLayer") != std::string::npos) c.third_party_layers.push_back(m);
            }
            // Multiple audit records can land per collector (e.g. IGCL emits per-bind notes
            // + the wrap-init record). Merge: keep the most informative one — prefer the
            // record with module list / init_wall data.
            auto it = std::find_if(collector_audits.begin(), collector_audits.end(),
                                   [&](const CollectorAcc& x) { return x.name == c.name; });
            if (it == collector_audits.end()) {
                collector_audits.push_back(std::move(c));
            } else {
                ++it->binding_records;
                if (c.init_wall_ns > 0 || !c.modules_loaded.empty()) {
                    auto bindings = it->binding_records;
                    *it = std::move(c);
                    it->binding_records = bindings;
                }
            }
        }
    }

    // ---- output --------------------------------------------------------------
    std::error_code ec;
    const auto file_size = fs::file_size(jsonl, ec);

    std::printf("# b70tools run summary\n");
    std::printf("file:           %s\n", jsonl.string().c_str());
    std::printf("size:           %s (%lld bytes)\n",
                human_bytes(static_cast<std::uint64_t>(file_size)).c_str(),
                static_cast<long long>(file_size));
    std::printf("lines:          %zu\n", counts.total);
    std::printf("session epochs: %u  (last = %u)\n", max_epoch + 1, max_epoch);
    if (first_ts && last_ts && last_ts > first_ts) {
        std::printf("duration (qpc): %s (first→last metric)\n",
                    human_ns(last_ts - first_ts).c_str());
    }
    std::printf("event counts:   metric=%zu  identity=%zu  state=%zu  disagree=%zu  epoch=%zu  fingerprint=%zu  audit=%zu\n",
                counts.metric_samples, counts.adapter_identities, counts.adapter_state_transitions,
                counts.disagreement_reports, counts.epoch_boundaries, counts.fingerprints, counts.collector_audits);

    // Adapters
    std::printf("\n## Adapters (%zu)\n", adapters.size());
    for (const auto& [id, a] : adapters) {
        std::printf("\n  %s\n", id.c_str());
        std::printf("    name:        %s\n", a.description.empty() ? "<unknown>" : a.description.c_str());
        std::printf("    LUID:        0x%016llx\n", static_cast<unsigned long long>(a.luid));
        std::printf("    PCI BDF:     %s\n", a.pci_bdf.empty() ? "<unknown>" : a.pci_bdf.c_str());
        if (!a.driver_uuid_decoded.empty()) {
            std::printf("    driver_uuid: %s (decoded: %s)\n",
                        a.driver_uuid_hex.c_str(), a.driver_uuid_decoded.c_str());
        } else if (!a.driver_uuid_hex.empty()) {
            std::printf("    driver_uuid: %s\n", a.driver_uuid_hex.c_str());
        }
        std::printf("    DedicatedVRAM: %s\n", human_bytes(a.dedicated_video_memory).c_str());
        std::printf("    SharedSystem:  %s\n", human_bytes(a.shared_system_memory).c_str());
        const auto tm_sum = a.dedicated_video_memory + a.shared_system_memory;
        std::printf("    Task-Mgr sum:  %s  (the \"48 GB\" mechanism on a 32 GB card)\n",
                    human_bytes(tm_sum).c_str());
        std::printf("    state reached: %s  (%zu transitions)\n",
                    a.highest_state.empty() ? "Unknown" : a.highest_state.c_str(),
                    a.state_transitions);
        if (!a.peaks.empty()) {
            std::printf("    peak metrics:\n");
            for (const auto& [pn, pm] : a.peaks) {
                std::printf("      %-42s ", pn.c_str());
                if      (pm.unit == "Bytes")            std::printf("%s",  human_bytes(static_cast<std::uint64_t>(pm.value)).c_str());
                else if (pm.unit == "Hertz")            std::printf("%s",  human_hz(pm.value).c_str());
                else if (pm.unit == "Celsius")          std::printf("%.1f C",  pm.value);
                else if (pm.unit == "Volts")            std::printf("%.3f V",  pm.value);
                else if (pm.unit == "Percent")          std::printf("%.1f %%", pm.value);
                else if (pm.unit == "Nanoseconds")      std::printf("%s",  human_ns(static_cast<std::uint64_t>(pm.value)).c_str());
                else if (pm.unit == "BytesPerSecond")   std::printf("%s/s",human_bytes(static_cast<std::uint64_t>(pm.value)).c_str());
                else                                    std::printf("%.4g", pm.value);
                std::printf("  via %s\n", pm.source.c_str());
            }
        }
    }

    // Did both adapters wake?
    std::printf("\n## Did adapters wake?\n");
    std::size_t woken = 0;
    for (const auto& [id, a] : adapters) {
        const bool woke = state_rank(a.highest_state) >= 2;
        std::printf("  %s  %s  (highest: %s)\n",
                    woke ? "[YES]" : "[no ]",
                    id.c_str(),
                    a.highest_state.empty() ? "Unknown" : a.highest_state.c_str());
        if (woke) ++woken;
    }
    std::printf("  --> %zu of %zu adapter(s) advanced past Unknown\n",
                woken, adapters.size());

    // Disagreements — grouped by (rule, adapter) for readability
    std::printf("\n## Disagreements (%zu total)\n", disagreements.size());
    if (disagreements.empty()) {
        std::printf("  (none recorded)\n");
    } else {
        struct Group { std::size_t count = 0; const DisagreementAcc* first = nullptr;
                       std::uint64_t first_ts = 0; std::uint64_t last_ts = 0; };
        std::map<std::string, Group> groups;
        for (const auto& d : disagreements) {
            std::string key = d.rule + "/" + d.adapter_id;
            auto& g = groups[key];
            ++g.count;
            if (!g.first) { g.first = &d; g.first_ts = d.timestamp_qpc; g.last_ts = d.timestamp_qpc; }
            if (d.timestamp_qpc > g.last_ts) g.last_ts = d.timestamp_qpc;
        }
        std::vector<const Group*> sorted;
        sorted.reserve(groups.size());
        for (const auto& [k, g] : groups) sorted.push_back(&g);
        std::sort(sorted.begin(), sorted.end(),
                  [](const Group* a, const Group* b) {
                      if (a->count != b->count) return a->count > b->count;
                      return a->first->rule < b->first->rule;
                  });
        std::printf("  %zu unique (rule, adapter) pairs:\n\n", groups.size());
        for (const auto* g : sorted) {
            std::printf("  [%zu]  %s  /  %s\n", g->count, g->first->rule.c_str(),
                        g->first->adapter_id.c_str());
            if (g->last_ts > g->first_ts) {
                const double span_s = (g->last_ts - g->first_ts) / 1e9;
                std::printf("        span: %.2f s\n", span_s);
            }
            if (!g->first->sources.empty()) {
                std::printf("        sources:");
                for (const auto& s : g->first->sources) std::printf(" %s", s.c_str());
                std::printf("\n");
            }
            std::printf("        %s\n\n", g->first->explanation.c_str());
        }
    }

    // Collectors
    std::printf("\n## Collectors (%zu)\n", collector_audits.size());
    if (collector_audits.empty()) {
        std::printf("  (no audit records)\n");
    } else {
        std::printf("  %-30s  %-15s  %-8s  %-9s  %5s  %-6s  notes\n",
                    "name", "declared", "init", "rss", "thr+", "mods+");
        std::printf("  %-30s  %-15s  %-8s  %-9s  %5s  %-6s  -----\n",
                    "----", "--------", "----", "---", "----", "-----");
        for (const auto& c : collector_audits) {
            const char* decl = c.declared_truly_passive ? "TrulyPassive"
                              : c.declared_driver_passive ? "DriverPassive"
                              : "?";
            const std::uint32_t thr_added = (c.threads_after > c.threads_before)
                                          ? c.threads_after - c.threads_before : 0;
            std::printf("  %-30s  %-15s  %-8s  %-9s  %5u  %-6zu  %s%s\n",
                        c.name.c_str(),
                        decl,
                        human_ns(c.init_wall_ns).c_str(),
                        human_rss_delta(c.rss_delta_bytes).c_str(),
                        thr_added,
                        c.modules_loaded.size(),
                        c.observed_undeclared ? "[UNDECLARED] " : "",
                        c.note.c_str());
            if (!c.third_party_layers.empty()) {
                std::printf("      third-party Vulkan layers loaded during init:");
                for (const auto& m : c.third_party_layers) std::printf(" %s", m.c_str());
                std::printf("\n");
            }
        }
    }

    // Observation cost
    std::printf("\n## Observation cost\n");
    std::int64_t rss_total = 0;
    std::uint64_t init_total_ns = 0;
    for (const auto& c : collector_audits) {
        rss_total += c.rss_delta_bytes;
        init_total_ns += c.init_wall_ns;
    }
    std::printf("  total RSS attributed to collector inits:  %s\n",  human_rss_delta(rss_total).c_str());
    std::printf("  total wall time for collector inits:      %s\n",  human_ns(init_total_ns).c_str());
    std::printf("  events on disk:                            %zu\n", counts.total);
    std::printf("  jsonl bytes on disk:                       %lld\n", static_cast<long long>(file_size));
    if (counts.total > 0)
        std::printf("  bytes/event:                              %.1f\n",
                    static_cast<double>(file_size) / static_cast<double>(counts.total));

    // Fingerprint
    std::printf("\n## Fingerprint\n");
    if (!fp.present) {
        std::printf("  (no DriverRuntimeFingerprint event recorded)\n");
    } else {
        std::printf("  Windows:        %s\n", fp.windows_build.c_str());
        std::printf("  BIOS:           %s (%s)\n", fp.bios_version.c_str(), fp.bios_date.c_str());
        std::printf("  Intel driver:   %s\n", fp.intel_driver_version.c_str());
        std::printf("  Vulkan runtime: %s\n", fp.vulkan_runtime.c_str());
        std::printf("  HAGS:           %s\n", fp.hags ? "enabled" : "disabled");
        std::printf("  WDAC:           %s%s\n",
                    fp.wdac ? "enforced" : "false",
                    fp.wdac ? "" : "  [known v1.5 bug: actually Enforced per msinfo32]");
        std::printf("  Above 4G:       %s\n", fp.a4g ? "on" : "off/unknown");
        if (!fp.per_adapter_notes.empty()) {
            std::printf("  per-adapter:\n");
            for (const auto& n : fp.per_adapter_notes) std::printf("    %s\n", n.c_str());
        }
    }

    // Warnings (cross-section)
    std::printf("\n## Warnings\n");
    std::size_t warn_count = 0;
    for (const auto& c : collector_audits) {
        if (c.observed_undeclared) {
            std::printf("  - collector %s: observed side-effects exceed declaration\n",
                        c.name.c_str());
            ++warn_count;
        }
        if (!c.third_party_layers.empty()) {
            std::printf("  - third-party Vulkan layer loaded via %s:", c.name.c_str());
            for (const auto& m : c.third_party_layers) std::printf(" %s", m.c_str());
            std::printf("\n");
            ++warn_count;
        }
    }
    for (const auto& d : disagreements) {
        if (d.rule == "expected_source_unavailable") {
            std::printf("  - source unavailable on this rig: %s for %s\n",
                        d.sources.empty() ? "?" : d.sources.front().c_str(),
                        d.adapter_id.c_str());
            ++warn_count;
        }
    }
    if (fp.present && !fp.wdac) {
        std::printf("  - WDAC self-detection unreliable (known v1.5 bug)\n");
        ++warn_count;
    }
    if (warn_count == 0) std::printf("  (none)\n");

    return 0;
}

}
