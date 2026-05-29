#include "tools/disagreements.h"

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

// Minimal duplication of summarize.cc helpers — small enough to keep local.

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

struct DisagreementGroup {
    std::string rule;
    std::string adapter_id;
    std::size_t count = 0;
    std::string first_explanation;
    std::vector<std::string> first_sources;
    std::uint64_t first_ts = 0;
    std::uint64_t last_ts = 0;
};

std::filesystem::path resolve_jsonl_path(const std::filesystem::path& arg) {
    if (std::filesystem::is_directory(arg)) {
        auto p = arg / "events.jsonl";
        if (std::filesystem::exists(p)) return p;
    }
    return arg;
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

std::string human_ns(std::uint64_t ns) {
    char buf[32];
    if (ns >= 1'000'000'000ull) std::snprintf(buf, sizeof(buf), "%.2f s",  ns / 1e9);
    else if (ns >= 1'000'000)   std::snprintf(buf, sizeof(buf), "%.1f ms", ns / 1e6);
    else if (ns >= 1'000)       std::snprintf(buf, sizeof(buf), "%.1f us", ns / 1e3);
    else                        std::snprintf(buf, sizeof(buf), "%llu ns", static_cast<unsigned long long>(ns));
    return buf;
}

}

int run_disagreements_command(const std::filesystem::path& arg) {
    namespace fs = std::filesystem;
    fs::path jsonl = resolve_jsonl_path(arg);
    if (!fs::exists(jsonl)) {
        std::fprintf(stderr, "disagreements: no events.jsonl at %s\n", jsonl.string().c_str());
        return 2;
    }
    std::ifstream in(jsonl, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "disagreements: cannot open %s\n", jsonl.string().c_str());
        return 2;
    }

    std::map<std::string, DisagreementGroup> groups;  // key = rule + "/" + adapter_id
    std::size_t total_lines = 0;
    std::size_t total_drs = 0;

    std::string line;
    while (std::getline(in, line)) {
        ++total_lines;
        if (line.empty()) continue;
        std::string_view sv(line);
        auto kind = find_string(sv, "k");
        if (!kind || *kind != "dr") continue;
        ++total_drs;

        auto rule  = find_string(sv, "rule").value_or("?");
        auto aid   = find_string(sv, "a").value_or("?");
        auto expl  = find_string(sv, "expl").value_or("");
        auto srcs  = find_string_array(sv, "srcs");
        auto ts    = static_cast<std::uint64_t>(find_int(sv, "t").value_or(0));

        std::string key = rule + "/" + aid;
        auto it = groups.find(key);
        if (it == groups.end()) {
            DisagreementGroup g;
            g.rule = rule;
            g.adapter_id = aid;
            g.count = 1;
            g.first_explanation = expl;
            g.first_sources = srcs;
            g.first_ts = ts;
            g.last_ts = ts;
            groups[key] = std::move(g);
        } else {
            ++it->second.count;
            if (ts > it->second.last_ts) it->second.last_ts = ts;
        }
    }

    std::printf("# disagreements summary\n");
    std::printf("file:   %s\n", jsonl.string().c_str());
    std::printf("lines:  %zu  (DisagreementReport events: %zu)\n", total_lines, total_drs);
    std::printf("classes: %zu unique (rule, adapter) pairs\n\n", groups.size());

    if (groups.empty()) {
        std::printf("(no disagreements recorded)\n");
        return 0;
    }

    std::vector<const DisagreementGroup*> sorted;
    sorted.reserve(groups.size());
    for (const auto& [k, g] : groups) sorted.push_back(&g);
    std::sort(sorted.begin(), sorted.end(),
              [](const DisagreementGroup* a, const DisagreementGroup* b) {
                  if (a->count != b->count) return a->count > b->count;
                  return a->rule < b->rule;
              });

    for (const auto* g : sorted) {
        std::printf("[%zu]  %s  /  %s\n", g->count, g->rule.c_str(), g->adapter_id.c_str());
        if (g->first_ts && g->last_ts && g->last_ts > g->first_ts) {
            std::printf("       span: %s\n", human_ns(g->last_ts - g->first_ts).c_str());
        }
        std::printf("       sources:");
        if (g->first_sources.empty()) std::printf(" -");
        for (const auto& s : g->first_sources) std::printf(" %s", s.c_str());
        std::printf("\n");
        std::printf("       %s\n\n", g->first_explanation.c_str());
    }

    return 0;
}

}
