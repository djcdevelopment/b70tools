#include "arbitrator/disagreement_rules.h"

#include "schema/enums.h"
#include "schema/events.h"

#include <variant>

namespace b70 {

DisagreementRules::DisagreementRules(EventBus* bus_for_emit)
    : bus_(bus_for_emit) {}

void DisagreementRules::note_adapter_dedicated_vram(const std::string& adapter_id,
                                                    std::uint64_t bytes) {
    dedicated_vram_[adapter_id] = bytes;
}

void DisagreementRules::on_identity(const AdapterIdentity& a) {
    if (a.dedicated_video_memory > 0) {
        dedicated_vram_[a.adapter_id] = a.dedicated_video_memory;
    }
}

static bool value_as_u64(const MetricValue& v, std::uint64_t& out) {
    if (std::holds_alternative<std::uint64_t>(v)) { out = std::get<std::uint64_t>(v); return true; }
    if (std::holds_alternative<std::int64_t>(v)) {
        auto i = std::get<std::int64_t>(v);
        if (i < 0) return false;
        out = static_cast<std::uint64_t>(i);
        return true;
    }
    if (std::holds_alternative<double>(v)) {
        double d = std::get<double>(v);
        if (!(d >= 0.0)) return false;
        out = static_cast<std::uint64_t>(d);
        return true;
    }
    return false;
}

bool DisagreementRules::check_48gb_pattern(const MetricSample& m) {
    if (m.semantic_domain != SemanticDomain::Memory) return false;
    if (m.metric_name.find("total") == std::string::npos &&
        m.metric_name.find("usage") == std::string::npos) return false;

    auto it = dedicated_vram_.find(m.adapter_id);
    if (it == dedicated_vram_.end() || it->second == 0) return false;

    std::uint64_t v = 0;
    if (!value_as_u64(m.value, v)) return false;

    const std::uint64_t threshold = it->second + it->second / 20;  // ×1.05
    if (v > threshold) {
        emit_report("48gb_pattern", m.adapter_id,
                    "VRAM total exceeds DedicatedVideoMemory * 1.05; source likely summing SharedSystemMemory",
                    m);
        return true;
    }
    return false;
}

bool DisagreementRules::check_nan_inf(const MetricSample& m) {
    if (m.flags & flags::SourceEmittedNaN) {
        emit_report("nan_injection", m.adapter_id,
                    "Source emitted NaN; coerced to Missing", m);
        return true;
    }
    if (m.flags & flags::SourceEmittedInf) {
        emit_report("inf_injection", m.adapter_id,
                    "Source emitted Inf; coerced to Missing", m);
        return true;
    }
    return false;
}

bool DisagreementRules::check_impossible_heap_budget(const MetricSample& m) {
    if (m.semantic_domain != SemanticDomain::Memory) return false;
    if (m.metric_name.find("budget") == std::string::npos) return false;
    auto it = dedicated_vram_.find(m.adapter_id);
    if (it == dedicated_vram_.end() || it->second == 0) return false;
    std::uint64_t v = 0;
    if (!value_as_u64(m.value, v)) return false;
    const std::uint64_t threshold = it->second + it->second / 2;  // ×1.5
    if (v > threshold) {
        emit_report("impossible_heap_budget", m.adapter_id,
                    "heapBudget > DedicatedVideoMemory * 1.5; LOCAL/NON_LOCAL likely conflated",
                    m);
        return true;
    }
    return false;
}

void DisagreementRules::emit_report(const std::string& rule,
                                    const std::string& adapter_id,
                                    const std::string& explanation,
                                    const MetricSample& m) {
    if (!bus_) return;
    DisagreementReport r;
    r.rule_name = rule;
    r.adapter_id = adapter_id;
    r.explanation = explanation;
    r.resulting_confidence = Confidence::Disagreed;
    r.involved_sources.emplace_back(std::string(to_string(m.source)));
    r.session_epoch = m.session_epoch;
    r.timestamp_qpc = m.timestamp_qpc;
    bus_->publish(r);
    ++reports_emitted_;
}

void DisagreementRules::on_metric(const MetricSample& m) {
    check_nan_inf(m);
    check_48gb_pattern(m);
    check_impossible_heap_budget(m);

    check_impossible_voltage(m);
    check_impossible_frequency(m);
    check_impossible_temperature(m);
    check_impossible_percent(m);
    check_impossible_bandwidth(m);

    // Silence-detection (narrow scope: IGCL only): on every metric arrival, update the
    // tracker for this (adapter, source, domain) and sweep all tracked keys for any that
    // have gone silent past threshold. Opportunistic check — no tick callback required.
    update_source_tracking(m);
    check_for_silence(m.timestamp_qpc, m.session_epoch);
}

namespace {
constexpr std::uint64_t kSilenceThresholdNs = 3'000'000'000ull;  // 3 ticks at 1 Hz

std::string track_key_for(const std::string& adapter_id, Source s, SemanticDomain d) {
    char tail[24];
    std::snprintf(tail, sizeof(tail), "|%u|%u",
                  static_cast<unsigned>(s), static_cast<unsigned>(d));
    return adapter_id + tail;
}
}  // namespace

void DisagreementRules::update_source_tracking(const MetricSample& m) {
    // Initial scope: IGCL only. Expand to other sources after operational validation.
    if (m.source != Source::IGCL_PowerTelemetry) return;
    if (m.adapter_id.empty()) return;

    const std::string key = track_key_for(m.adapter_id, m.source, m.semantic_domain);
    auto& st = source_tracking_[key];
    st.adapter_id = m.adapter_id;
    st.source = m.source;
    st.domain = m.semantic_domain;

    if (st.silent) {
        emit_source_resumed(st, m.timestamp_qpc, m.session_epoch);
        st.silent = false;
        st.silence_first_detected_ts = 0;
    }
    st.last_seen_ts = m.timestamp_qpc;
}

void DisagreementRules::check_for_silence(std::uint64_t now_ts, std::uint32_t epoch) {
    for (auto& [key, st] : source_tracking_) {
        (void)key;
        if (st.silent) continue;                     // one report per episode
        if (st.last_seen_ts == 0) continue;          // never seen, no baseline
        if (now_ts <= st.last_seen_ts) continue;     // out-of-order or same tick
        if (now_ts - st.last_seen_ts >= kSilenceThresholdNs) {
            emit_source_silent(st, now_ts, epoch);
            st.silent = true;
            st.silence_first_detected_ts = now_ts;
        }
    }
}

void DisagreementRules::emit_source_silent(const TrackState& st,
                                            std::uint64_t now_ts,
                                            std::uint32_t epoch) {
    if (!bus_) return;
    DisagreementReport r;
    r.rule_name = "previously_reporting_source_went_silent";
    r.adapter_id = st.adapter_id;
    r.resulting_confidence = Confidence::Low;
    char tmp[416];
    const double silent_for_s = (now_ts - st.last_seen_ts) / 1e9;
    std::snprintf(tmp, sizeof(tmp),
                  "%s previously reported %s telemetry for %s, then stopped emitting "
                  "for %.2f s (>=3 consecutive ticks at 1 Hz cadence); "
                  "flags: source_degraded, missing_after_present, possible_contention. "
                  "NOT proof of adapter failure or workload failure — only proof that "
                  "the observer stopped reporting. Continue trusting DXGI/Vulkan/D3DKMT "
                  "signals for this adapter. Last-seen IGCL values must be treated as stale.",
                  std::string(to_string(st.source)).c_str(),
                  std::string(to_string(st.domain)).c_str(),
                  st.adapter_id.c_str(),
                  silent_for_s);
    r.explanation = tmp;
    r.involved_sources.emplace_back(std::string(to_string(st.source)));
    r.session_epoch = epoch;
    r.timestamp_qpc = now_ts;
    bus_->publish(r);
    ++reports_emitted_;
}

void DisagreementRules::emit_source_resumed(const TrackState& st,
                                             std::uint64_t resumed_ts,
                                             std::uint32_t epoch) {
    if (!bus_) return;
    DisagreementReport r;
    r.rule_name = "source_resumed";
    r.adapter_id = st.adapter_id;
    r.resulting_confidence = Confidence::Medium;
    char tmp[320];
    const double silence_duration_s = (resumed_ts - st.last_seen_ts) / 1e9;
    std::snprintf(tmp, sizeof(tmp),
                  "%s for %s on %s resumed after %.2f s of silence; "
                  "first resumed sample at qpc=%llu. Treat earlier silence-period values as stale.",
                  std::string(to_string(st.source)).c_str(),
                  std::string(to_string(st.domain)).c_str(),
                  st.adapter_id.c_str(),
                  silence_duration_s,
                  static_cast<unsigned long long>(resumed_ts));
    r.explanation = tmp;
    r.involved_sources.emplace_back(std::string(to_string(st.source)));
    r.session_epoch = epoch;
    r.timestamp_qpc = resumed_ts;
    bus_->publish(r);
    ++reports_emitted_;
}

void DisagreementRules::on_epoch_boundary(const SessionEpochBoundary&) {
    // Per spec: silence tracker resets when the session epoch changes.
    // The new epoch's first samples re-establish baseline; no carry-over silence claims.
    source_tracking_.clear();
}

static bool value_as_double(const MetricValue& v, double& out) {
    if (std::holds_alternative<std::uint64_t>(v)) { out = static_cast<double>(std::get<std::uint64_t>(v)); return true; }
    if (std::holds_alternative<std::int64_t>(v))  { out = static_cast<double>(std::get<std::int64_t>(v));  return true; }
    if (std::holds_alternative<double>(v))        { out = std::get<double>(v); return true; }
    return false;
}

bool DisagreementRules::check_impossible_voltage(const MetricSample& m) {
    if (m.unit != Unit::Volts) return false;
    double v = 0;
    if (!value_as_double(m.value, v)) return false;
    if (v >= 0.0 && v <= 2.0) return false;
    char tmp[192];
    std::snprintf(tmp, sizeof(tmp),
                  "physically_impossible: voltage %.3f V is outside [0, 2.0] V envelope for GPU sensors "
                  "(metric=%s source=%s) — raw value preserved; source_degraded",
                  v, m.metric_name.c_str(), std::string(to_string(m.source)).c_str());
    emit_report("physically_impossible_voltage", m.adapter_id, tmp, m);
    return true;
}

bool DisagreementRules::check_impossible_frequency(const MetricSample& m) {
    if (m.unit != Unit::Hertz) return false;
    if (m.semantic_domain != SemanticDomain::Frequency) return false;
    double v = 0;
    if (!value_as_double(m.value, v)) return false;
    // Allow VRAM frequencies up to ~3.5 GHz (GDDR6/6X effective); GPU core freqs cap well under 5 GHz.
    const bool is_vram = m.metric_name.find("vram.") == 0;
    const double ceiling = is_vram ? 4.0e9 : 5.0e9;
    if (v >= 0.0 && v <= ceiling) return false;
    char tmp[224];
    std::snprintf(tmp, sizeof(tmp),
                  "physically_impossible: %s frequency %.3f GHz exceeds %.1f GHz envelope "
                  "(metric=%s source=%s) — raw value preserved; source_degraded",
                  is_vram ? "VRAM" : "GPU", v / 1e9, ceiling / 1e9,
                  m.metric_name.c_str(), std::string(to_string(m.source)).c_str());
    emit_report("physically_impossible_frequency", m.adapter_id, tmp, m);
    return true;
}

bool DisagreementRules::check_impossible_temperature(const MetricSample& m) {
    if (m.unit != Unit::Celsius) return false;
    double v = 0;
    if (!value_as_double(m.value, v)) return false;
    if (v >= -10.0 && v <= 120.0) return false;
    char tmp[192];
    std::snprintf(tmp, sizeof(tmp),
                  "physically_impossible: temperature %.1f C is outside [-10, 120] C envelope "
                  "(metric=%s source=%s) — raw value preserved; source_degraded",
                  v, m.metric_name.c_str(), std::string(to_string(m.source)).c_str());
    emit_report("physically_impossible_temperature", m.adapter_id, tmp, m);
    return true;
}

bool DisagreementRules::check_impossible_percent(const MetricSample& m) {
    if (m.unit != Unit::Percent) return false;
    double v = 0;
    if (!value_as_double(m.value, v)) return false;
    if (v >= -1.0 && v <= 110.0) return false;
    char tmp[192];
    std::snprintf(tmp, sizeof(tmp),
                  "structurally_implausible: percentage %.2f%% outside [-1, 110] envelope "
                  "(metric=%s source=%s) — raw value preserved; source_degraded",
                  v, m.metric_name.c_str(), std::string(to_string(m.source)).c_str());
    emit_report(v < 0 ? "structurally_implausible_negative_utilization"
                       : "structurally_implausible_percent_overshoot",
                m.adapter_id, tmp, m);
    return true;
}

bool DisagreementRules::check_impossible_bandwidth(const MetricSample& m) {
    if (m.unit != Unit::BytesPerSecond) return false;
    double v = 0;
    if (!value_as_double(m.value, v)) return false;
    constexpr double kBwCeilingBps = 10.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0;  // 10 TiB/s
    if (v >= 0.0 && v <= kBwCeilingBps) return false;
    char tmp[224];
    std::snprintf(tmp, sizeof(tmp),
                  "physically_impossible: bandwidth %.2e B/s exceeds 10 TiB/s sanity ceiling "
                  "(metric=%s source=%s) — raw value preserved; source_degraded",
                  v, m.metric_name.c_str(), std::string(to_string(m.source)).c_str());
    emit_report("physically_impossible_bandwidth", m.adapter_id, tmp, m);
    return true;
}

}
