#include "tools/verdict.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace b70 {

namespace {

// --- minimal JSON field extraction (b70tools-compact-format-aware) ----------
// Reused pattern from summarize.cc / adapters.cc — see those files for the
// rationale (b70tools controls the writer; no full JSON parser needed).
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
    needle.push_back('"'); needle.append(key); needle.append("\":\"");
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
    needle.push_back('"'); needle.append(key); needle.append("\":");
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
    needle.push_back('"'); needle.append(key); needle.append("\":");
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

std::filesystem::path resolve_jsonl(const std::filesystem::path& arg) {
    if (std::filesystem::is_directory(arg)) {
        auto p = arg / "events.jsonl";
        if (std::filesystem::exists(p)) return p;
        auto p2 = arg / "telemetry" / "events.jsonl";
        if (std::filesystem::exists(p2)) return p2;
    }
    return arg;
}

// JSON helpers — flat output, no nesting; keeps the consumer (PowerShell
// ConvertFrom-Json + the eval harness) trivially parseable.
void json_escape(std::string& out, std::string_view s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\n': out.append("\\n"); break;
            case '\r': out.append("\\r"); break;
            case '\t': out.append("\\t"); break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out.append(buf);
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

struct Series {
    double first = 0.0, last = 0.0;
    std::uint64_t first_ts = 0, last_ts = 0;
    std::size_t n = 0;

    void add_clamped_to_window(double v, std::uint64_t ts,
                               std::uint64_t window_start, std::uint64_t window_end) {
        if (window_start && ts < window_start) return;
        if (window_end   && ts > window_end)   return;
        if (n == 0) { first = v; first_ts = ts; }
        last = v; last_ts = ts;
        ++n;
    }
};

}  // namespace

int run_verdict_command(const std::filesystem::path& arg, const VerdictOptions& opts) {
    namespace fs = std::filesystem;
    fs::path jsonl = resolve_jsonl(arg);
    if (!fs::exists(jsonl)) {
        std::fprintf(stderr, "verdict: no events.jsonl at %s\n", jsonl.string().c_str());
        return 3;
    }
    std::ifstream in(jsonl, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "verdict: cannot open %s\n", jsonl.string().c_str());
        return 3;
    }

    // Pass 1: find the timestamp window [start, end] from the last "ms" event.
    std::uint64_t file_first_ts = 0;
    std::uint64_t file_last_ts  = 0;
    {
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            std::string_view sv(line);
            auto kind = find_string(sv, "k");
            if (!kind || *kind != "ms") continue;
            auto t = find_int(sv, "t");
            if (!t || *t <= 0) continue;
            const auto ts = static_cast<std::uint64_t>(*t);
            if (file_first_ts == 0 || ts < file_first_ts) file_first_ts = ts;
            if (ts > file_last_ts) file_last_ts = ts;
        }
        in.clear();
        in.seekg(0);
    }

    if (file_last_ts == 0 || file_first_ts == 0) {
        std::fprintf(stderr, "verdict: events.jsonl has no MetricSample timestamps\n");
        return 3;
    }

    // Window: use the last opts.window_s seconds. If window_s <= 0, use full file.
    std::uint64_t window_start = 0;
    std::uint64_t window_end   = file_last_ts;
    if (opts.window_s > 0) {
        const auto width_ns = static_cast<std::uint64_t>(opts.window_s * 1e9);
        if (file_last_ts > width_ns) {
            window_start = file_last_ts - width_ns;
        } else {
            window_start = file_first_ts;
        }
    } else {
        window_start = file_first_ts;
    }

    // Pass 2: accumulate the metrics we need within the window.
    Series host_used;
    Series host_avail;
    Series host_total;
    std::map<std::string, Series> per_adapter_energy;        // adapter -> energy counter (J)
    std::map<std::string, Series> per_adapter_activity_rc;   // adapter -> render_compute counter (ns)
    std::map<std::string, Series> per_adapter_vram_local;    // adapter -> system-wide local VRAM commit (bytes)
    std::map<std::string, Series> per_adapter_vram_nonlocal; // adapter -> system-wide non_local commit (bytes)
    std::map<std::string, std::string> adapter_descs;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string_view sv(line);
        auto kind = find_string(sv, "k");
        if (!kind) continue;

        if (*kind == "ai") {
            auto id = find_string(sv, "a").value_or("");
            auto d  = find_string(sv, "desc").value_or("");
            if (!id.empty()) adapter_descs[id] = d;
            continue;
        }
        if (*kind != "ms") continue;

        auto t = find_int(sv, "t");
        if (!t || *t <= 0) continue;
        const auto ts = static_cast<std::uint64_t>(*t);

        auto name = find_string(sv, "n").value_or("");
        if (name.empty()) continue;
        auto v = find_double(sv, "v");
        if (!v) continue;
        auto adapter = find_string(sv, "a").value_or("");

        if (name == "host.memory.used_bytes") {
            host_used.add_clamped_to_window(*v, ts, window_start, window_end);
        } else if (name == "host.memory.available_bytes") {
            host_avail.add_clamped_to_window(*v, ts, window_start, window_end);
        } else if (name == "host.memory.total_bytes") {
            host_total.add_clamped_to_window(*v, ts, window_start, window_end);
        } else if (!adapter.empty() && name == "gpu.energy_j_counter") {
            per_adapter_energy[adapter].add_clamped_to_window(*v, ts, window_start, window_end);
        } else if (!adapter.empty() && name == "gpu.activity.render_compute_counter") {
            per_adapter_activity_rc[adapter].add_clamped_to_window(*v, ts, window_start, window_end);
        } else if (!adapter.empty() && name == "gpu.adapter.vram.local.bytes_committed") {
            per_adapter_vram_local[adapter].add_clamped_to_window(*v, ts, window_start, window_end);
        } else if (!adapter.empty() && name == "gpu.adapter.vram.non_local.bytes_committed") {
            per_adapter_vram_nonlocal[adapter].add_clamped_to_window(*v, ts, window_start, window_end);
        }
    }

    // --- evaluate gates --------------------------------------------------------
    const double GB = static_cast<double>(1ull << 30);

    // Host memory: latest reading in the window.
    const double host_used_gb = host_used.n ? (host_used.last / GB) : -1.0;
    const bool   host_ram_known = (host_used.n > 0);
    const bool   host_ram_ok    = host_ram_known && (host_used_gb <= opts.max_host_used_gb);

    // Healthy adapter work signature: average power + activity over the window.
    const auto& hi = opts.healthy_adapter;
    double avg_w = 0.0, activity_pct = 0.0;
    double work_window_s = 0.0;
    bool   work_known    = false;
    {
        auto eIt = per_adapter_energy.find(hi);
        auto aIt = per_adapter_activity_rc.find(hi);
        if (eIt != per_adapter_energy.end() && eIt->second.n >= 2 &&
            aIt != per_adapter_activity_rc.end() && aIt->second.n >= 2 &&
            eIt->second.last_ts > eIt->second.first_ts) {
            work_known    = true;
            work_window_s = (eIt->second.last_ts - eIt->second.first_ts) / 1e9;
            const double dJ  = eIt->second.last - eIt->second.first;
            const double dAct = aIt->second.last - aIt->second.first;
            if (work_window_s > 0) {
                avg_w         = dJ / work_window_s;
                activity_pct  = (dAct / 1e9) / work_window_s * 100.0;
            }
        }
    }
    const bool work_ok = work_known && (avg_w >= opts.min_card0_avg_w) &&
                                       (activity_pct >= opts.min_card0_activity_pct);

    // Per-adapter VRAM (system-wide via D3DKMT_QS — once that collector works).
    // For now, presence is optional; absence does not fail the verdict but is
    // noted in the report.
    struct AdapterVram {
        std::string adapter;
        bool   have_local    = false;
        bool   have_nonlocal = false;
        double local_first_gb = 0, local_last_gb  = 0;
        double non_first_gb  = 0, non_last_gb    = 0;
    };
    std::vector<AdapterVram> vram_view;
    {
        std::map<std::string, AdapterVram> by;
        for (auto& [aid, s] : per_adapter_vram_local) {
            auto& a = by[aid]; a.adapter = aid;
            if (s.n) { a.have_local = true; a.local_first_gb = s.first / GB; a.local_last_gb = s.last / GB; }
        }
        for (auto& [aid, s] : per_adapter_vram_nonlocal) {
            auto& a = by[aid]; a.adapter = aid;
            if (s.n) { a.have_nonlocal = true; a.non_first_gb = s.first / GB; a.non_last_gb = s.last / GB; }
        }
        for (auto& [_, a] : by) vram_view.push_back(a);
        std::sort(vram_view.begin(), vram_view.end(),
                  [](const AdapterVram& a, const AdapterVram& b) { return a.adapter < b.adapter; });
    }

    // VRAM gate evaluation (applied if PDH cross-process VRAM is available for
    // at least 2 adapters). These are the primary load-distribution checks.
    bool   vram_known = false;
    bool   vram_ok    = true;
    double vram_min_local_gb   = 0.0;
    double vram_max_local_gb   = 0.0;
    double vram_asymmetry      = 1.0;
    double vram_max_nonlocal_gb = 0.0;
    std::string vram_max_nonlocal_adapter;
    std::vector<std::string> vram_issues;
    {
        std::vector<std::pair<std::string, double>> adapter_local_last_gb;
        std::vector<std::pair<std::string, double>> adapter_nonlocal_last_gb;
        for (const auto& a : vram_view) {
            if (a.have_local)    adapter_local_last_gb.emplace_back(a.adapter, a.local_last_gb);
            if (a.have_nonlocal) adapter_nonlocal_last_gb.emplace_back(a.adapter, a.non_last_gb);
        }
        if (adapter_local_last_gb.size() >= 2) {
            vram_known = true;
            vram_min_local_gb = adapter_local_last_gb.front().second;
            vram_max_local_gb = adapter_local_last_gb.front().second;
            for (const auto& [_, v] : adapter_local_last_gb) {
                vram_min_local_gb = std::min(vram_min_local_gb, v);
                vram_max_local_gb = std::max(vram_max_local_gb, v);
            }
            for (const auto& [aid, v] : adapter_nonlocal_last_gb) {
                if (v > vram_max_nonlocal_gb) { vram_max_nonlocal_gb = v; vram_max_nonlocal_adapter = aid; }
            }
            vram_asymmetry = (vram_min_local_gb > 0.01) ? (vram_max_local_gb / vram_min_local_gb)
                                                       : std::numeric_limits<double>::infinity();

            if (opts.min_per_card_local_gb > 0) {
                for (const auto& [aid, v] : adapter_local_last_gb) {
                    if (v < opts.min_per_card_local_gb) {
                        char tmp[300];
                        std::snprintf(tmp, sizeof(tmp),
                                      "[vram] %s local commit %.2f GB below %.2f GB — card has no model layers (broken split or no workload)",
                                      aid.c_str(), v, opts.min_per_card_local_gb);
                        vram_issues.emplace_back(tmp);
                        vram_ok = false;
                    }
                }
            }
            if (opts.max_asymmetry_ratio > 0 && vram_min_local_gb > 0.01 &&
                vram_asymmetry > opts.max_asymmetry_ratio) {
                char tmp[300];
                std::snprintf(tmp, sizeof(tmp),
                              "[vram] asymmetry %.2fx exceeds %.2fx (max=%.2f GB / min=%.2f GB) — layer-split failed to balance",
                              vram_asymmetry, opts.max_asymmetry_ratio,
                              vram_max_local_gb, vram_min_local_gb);
                vram_issues.emplace_back(tmp);
                vram_ok = false;
            }
            if (opts.max_per_card_nonlocal_gb > 0) {
                for (const auto& [aid, v] : adapter_nonlocal_last_gb) {
                    if (v > opts.max_per_card_nonlocal_gb) {
                        char tmp[300];
                        std::snprintf(tmp, sizeof(tmp),
                                      "[vram] %s non_local (Shared GPU Memory) commit %.2f GB exceeds %.2f GB — model spilled to host RAM via PCIe",
                                      aid.c_str(), v, opts.max_per_card_nonlocal_gb);
                        vram_issues.emplace_back(tmp);
                        vram_ok = false;
                    }
                }
            }
        }
    }

    // Verdict order: host-RAM (mandatory safety floor) → vram-distribution
    // (primary load gate) → card-0 activity (advisory; weaker signal).
    int exit_code = 0;
    std::string status;
    std::vector<std::string> issues;

    if (!host_ram_known) {
        status = "insufficient-data";
        issues.emplace_back("no host.memory.* samples in the window — b70tools host_memory collector inactive");
        exit_code = 3;
    } else if (!host_ram_ok) {
        status = "broken";
        char tmp[200];
        std::snprintf(tmp, sizeof(tmp),
                      "[host-ram] used %.2f GB exceeds safety threshold %.2f GB — Shared GPU Memory fallback suspected; refuse to proceed",
                      host_used_gb, opts.max_host_used_gb);
        issues.emplace_back(tmp);
        exit_code = 2;
    } else if (vram_known && !vram_ok) {
        status = "broken";
        for (const auto& s : vram_issues) issues.emplace_back(s);
        exit_code = 2;
    } else if (!vram_known && work_known && !work_ok) {
        // VRAM unavailable; fall back to IGCL activity check.
        status = "broken";
        char tmp[300];
        std::snprintf(tmp, sizeof(tmp),
                      "[card-work] %s avg=%.1fW (>=%.1f), activity=%.1f%% (>=%.1f) over %.1fs — card 0 idle: model on broken-IGCL card only OR no workload",
                      hi.c_str(), avg_w, opts.min_card0_avg_w, activity_pct, opts.min_card0_activity_pct, work_window_s);
        issues.emplace_back(tmp);
        exit_code = 2;
    } else if (!vram_known && !work_known) {
        status = "insufficient-data";
        issues.emplace_back("no PDH per-adapter VRAM samples AND no IGCL energy samples in the window — neither gate can be evaluated");
        exit_code = 3;
    } else {
        status = "healthy";
        exit_code = 0;
    }

    // --- output ----------------------------------------------------------------
    if (opts.emit_json) {
        std::string j;
        j.reserve(1024);
        j.push_back('{');
        j.append("\"status\":"); json_escape(j, status); j.push_back(',');
        j.append("\"ok\":");      j.append(exit_code == 0 ? "true" : "false"); j.push_back(',');
        j.append("\"exit_code\":"); j.append(std::to_string(exit_code)); j.push_back(',');
        j.append("\"events_path\":"); json_escape(j, jsonl.string()); j.push_back(',');
        j.append("\"window_s\":");
        {
            char b[40];
            const double full_s = (file_last_ts - window_start) / 1e9;
            std::snprintf(b, sizeof(b), "%.2f", full_s);
            j.append(b);
        }
        j.push_back(',');

        // host
        j.append("\"host_memory\":{");
        j.append("\"used_gb\":");
        { char b[40]; std::snprintf(b, sizeof(b), "%.2f", host_used_gb); j.append(b); }
        j.push_back(','); j.append("\"threshold_gb\":");
        { char b[40]; std::snprintf(b, sizeof(b), "%.2f", opts.max_host_used_gb); j.append(b); }
        j.push_back(','); j.append("\"known\":"); j.append(host_ram_known ? "true" : "false");
        j.append("},");

        // healthy adapter work
        j.append("\"healthy_adapter_work\":{");
        j.append("\"adapter\":"); json_escape(j, hi); j.push_back(',');
        j.append("\"known\":"); j.append(work_known ? "true" : "false"); j.push_back(',');
        j.append("\"avg_w\":");
        { char b[40]; std::snprintf(b, sizeof(b), "%.2f", avg_w); j.append(b); }
        j.push_back(','); j.append("\"activity_pct\":");
        { char b[40]; std::snprintf(b, sizeof(b), "%.2f", activity_pct); j.append(b); }
        j.push_back(','); j.append("\"window_s\":");
        { char b[40]; std::snprintf(b, sizeof(b), "%.2f", work_window_s); j.append(b); }
        j.append("},");

        // per-adapter VRAM
        j.append("\"per_adapter_vram\":[");
        for (std::size_t i = 0; i < vram_view.size(); ++i) {
            if (i) j.push_back(',');
            const auto& a = vram_view[i];
            j.push_back('{');
            j.append("\"adapter\":"); json_escape(j, a.adapter); j.push_back(',');
            j.append("\"local_first_gb\":");
            { char b[40]; std::snprintf(b, sizeof(b), "%.2f", a.local_first_gb); j.append(b); }
            j.push_back(','); j.append("\"local_last_gb\":");
            { char b[40]; std::snprintf(b, sizeof(b), "%.2f", a.local_last_gb); j.append(b); }
            j.push_back(','); j.append("\"non_local_first_gb\":");
            { char b[40]; std::snprintf(b, sizeof(b), "%.2f", a.non_first_gb); j.append(b); }
            j.push_back(','); j.append("\"non_local_last_gb\":");
            { char b[40]; std::snprintf(b, sizeof(b), "%.2f", a.non_last_gb); j.append(b); }
            j.push_back(','); j.append("\"have_local\":");    j.append(a.have_local    ? "true" : "false");
            j.push_back(','); j.append("\"have_nonlocal\":"); j.append(a.have_nonlocal ? "true" : "false");
            j.push_back('}');
        }
        j.append("],");

        // issues
        j.append("\"issues\":[");
        for (std::size_t i = 0; i < issues.size(); ++i) {
            if (i) j.push_back(',');
            json_escape(j, issues[i]);
        }
        j.append("]");
        j.push_back('}');
        std::printf("%s\n", j.c_str());
    } else {
        std::printf("# b70tools verdict\n");
        std::printf("status: %s (exit=%d)\n", status.c_str(), exit_code);
        std::printf("file:   %s\n", jsonl.string().c_str());
        std::printf("window: last %.1f s (full span %.1f s)\n",
                    opts.window_s,
                    (file_last_ts - file_first_ts) / 1e9);
        std::printf("\nhost memory:\n");
        if (host_ram_known) {
            std::printf("  used = %.2f GB / threshold %.2f GB (latest in window; n=%zu)\n",
                        host_used_gb, opts.max_host_used_gb, host_used.n);
        } else {
            std::printf("  (no host.memory samples; collector inactive)\n");
        }
        std::printf("\nhealthy adapter work (%s):\n", hi.c_str());
        if (work_known) {
            std::printf("  avg power = %.2f W (>=%.1f), activity = %.2f%% (>=%.1f) over %.1f s\n",
                        avg_w, opts.min_card0_avg_w, activity_pct, opts.min_card0_activity_pct, work_window_s);
        } else {
            std::printf("  (no IGCL samples in window for this adapter)\n");
        }
        std::printf("\nper-adapter VRAM (system-wide, D3DKMT_QS):\n");
        if (vram_view.empty()) {
            std::printf("  (no gpu.adapter.vram.* samples — D3DKMT_QUERYSTATISTICS not emitting yet)\n");
        } else {
            for (const auto& a : vram_view) {
                std::printf("  %s  local: %.2f → %.2f GB  non_local: %.2f → %.2f GB\n",
                            a.adapter.c_str(),
                            a.local_first_gb, a.local_last_gb,
                            a.non_first_gb,  a.non_last_gb);
            }
        }
        if (!issues.empty()) {
            std::printf("\nissues:\n");
            for (const auto& s : issues) std::printf("  - %s\n", s.c_str());
        }
    }

    return exit_code;
}

}
