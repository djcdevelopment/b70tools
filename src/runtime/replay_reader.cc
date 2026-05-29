#include "runtime/replay_reader.h"

#include "schema/compact_format.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace b70 {

namespace {

std::optional<std::string_view> find_kind(std::string_view line) {
    const std::string_view needle = R"("k":")";
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    auto q = line.find('"', p);
    if (q == std::string_view::npos) return std::nullopt;
    return line.substr(p, q - p);
}

std::optional<std::string> extract_string_field(std::string_view line, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 4);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":\"");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    std::string out;
    while (p < line.size()) {
        char c = line[p];
        if (c == '"') break;
        if (c == '\\' && p + 1 < line.size()) {
            char e = line[p + 1];
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                default: out.push_back(e); break;
            }
            p += 2;
            continue;
        }
        out.push_back(c);
        ++p;
    }
    return out;
}

std::optional<std::uint64_t> extract_u64_field(std::string_view line, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 3);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    auto start = p;
    while (p < line.size() && (line[p] == '-' || line[p] == '+' ||
                               (line[p] >= '0' && line[p] <= '9') ||
                               line[p] == '.' || line[p] == 'e' || line[p] == 'E')) {
        ++p;
    }
    std::uint64_t v = 0;
    auto r = std::from_chars(line.data() + start, line.data() + p, v);
    if (r.ec != std::errc{}) return std::nullopt;
    return v;
}

std::optional<double> extract_f64_field(std::string_view line, std::string_view key) {
    std::string needle;
    needle.reserve(key.size() + 3);
    needle.push_back('"');
    needle.append(key);
    needle.append("\":");
    auto p = line.find(needle);
    if (p == std::string_view::npos) return std::nullopt;
    p += needle.size();
    if (p < line.size() && line[p] == 'n') return std::nullopt;  // null
    auto start = p;
    while (p < line.size() && (line[p] == '-' || line[p] == '+' ||
                               (line[p] >= '0' && line[p] <= '9') ||
                               line[p] == '.' || line[p] == 'e' || line[p] == 'E')) {
        ++p;
    }
    std::string tmp(line.data() + start, p - start);
    try {
        return std::stod(tmp);
    } catch (...) {
        return std::nullopt;
    }
}

AdapterState parse_adapter_state(const std::string& s) {
    if (s == "Unknown")                return AdapterState::Unknown;
    if (s == "Idle")                   return AdapterState::Idle;
    if (s == "Awake")                  return AdapterState::Awake;
    if (s == "ActiveCompute")          return AdapterState::ActiveCompute;
    if (s == "SuspectedComputeHidden") return AdapterState::SuspectedComputeHidden;
    if (s == "PostTDR")                return AdapterState::PostTDR;
    if (s == "Lost")                   return AdapterState::Lost;
    if (s == "Reenumerating")          return AdapterState::Reenumerating;
    return AdapterState::Unknown;
}

bool parse_metric(std::string_view line, EventBus& bus) {
    MetricSample m;
    auto n = extract_string_field(line, "n");
    auto a = extract_string_field(line, "a");
    if (!n || !a) return false;
    m.metric_name = *n;
    m.adapter_id  = *a;
    if (auto e = extract_u64_field(line, "e"); e) m.session_epoch = static_cast<std::uint32_t>(*e);
    if (auto t = extract_u64_field(line, "t"); t) m.timestamp_qpc = *t;
    if (auto l = extract_u64_field(line, "l"); l) m.poll_latency_ns = *l;
    if (auto w = extract_u64_field(line, "w"); w) m.sampling_window_ns = *w;
    if (auto g = extract_u64_field(line, "g"); g) m.flags = static_cast<std::uint32_t>(*g);
    m.source = Source::ReplayReader;
    if (auto f = extract_f64_field(line, "v"); f) {
        m.value = *f;
    }
    bus.publish(m);
    return true;
}

bool parse_state_transition(std::string_view line, EventBus& bus) {
    AdapterStateTransition t;
    auto a    = extract_string_field(line, "a");
    auto from = extract_string_field(line, "from");
    auto to   = extract_string_field(line, "to");
    auto r    = extract_string_field(line, "r");
    if (!a) return false;
    t.adapter_id = *a;
    if (from) t.from = parse_adapter_state(*from);
    if (to)   t.to   = parse_adapter_state(*to);
    if (r)    t.reason = *r;
    if (auto e = extract_u64_field(line, "e"); e) t.session_epoch = static_cast<std::uint32_t>(*e);
    if (auto ts = extract_u64_field(line, "t"); ts) t.timestamp_qpc = *ts;
    bus.publish(t);
    return true;
}

bool parse_identity(std::string_view line, EventBus& bus) {
    AdapterIdentity ai;
    auto a = extract_string_field(line, "a");
    if (!a) return false;
    ai.adapter_id = *a;
    if (auto d = extract_string_field(line, "desc"); d) ai.description = *d;
    if (auto bdf = extract_string_field(line, "bdf"); bdf) ai.pci_bdf = *bdf;
    if (auto luid = extract_u64_field(line, "luid"); luid) ai.luid = *luid;
    if (auto dvm = extract_u64_field(line, "dvm"); dvm) ai.dedicated_video_memory = *dvm;
    if (auto ssm = extract_u64_field(line, "ssm"); ssm) ai.shared_system_memory = *ssm;
    if (auto e = extract_u64_field(line, "e"); e) ai.session_epoch = static_cast<std::uint32_t>(*e);
    if (auto t = extract_u64_field(line, "t"); t) ai.timestamp_qpc = *t;
    bus.publish(ai);
    return true;
}

}

ReplayStats replay_jsonl(const std::filesystem::path& jsonl_path, EventBus& bus) {
    ReplayStats st;
    std::ifstream in(jsonl_path, std::ios::binary);
    if (!in) return st;

    std::string line;
    while (std::getline(in, line)) {
        ++st.lines_total;
        std::string_view sv(line);
        if (sv.empty()) continue;
        auto kind = find_kind(sv);
        if (!kind) { ++st.lines_failed; continue; }

        bool ok = false;
        if (*kind == compact::kKindMetric)          { ok = parse_metric(sv, bus);            if (ok) ++st.metric_samples; }
        else if (*kind == compact::kKindStateTrans) { ok = parse_state_transition(sv, bus);  if (ok) ++st.state_transitions; }
        else if (*kind == compact::kKindIdentity)   { ok = parse_identity(sv, bus);          if (ok) ++st.identities; }
        else if (*kind == compact::kKindDisagreement) { ok = true; ++st.disagreements; }
        else if (*kind == compact::kKindEpoch)      { ok = true; ++st.epoch_boundaries; }
        else if (*kind == compact::kKindFingerprint){ ok = true; ++st.fingerprints; }
        else if (*kind == compact::kKindAudit)      { ok = true; ++st.audits; }
        else                                        { ok = false; }

        if (ok) ++st.lines_parsed;
        else    ++st.lines_failed;
    }
    return st;
}

}
