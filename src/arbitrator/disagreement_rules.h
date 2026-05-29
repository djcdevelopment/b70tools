#pragma once

#include "bus/event_bus.h"
#include "schema/metric_sample.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace b70 {

class DisagreementRules : public EventSink {
public:
    explicit DisagreementRules(EventBus* bus_for_emit);

    void note_adapter_dedicated_vram(const std::string& adapter_id,
                                     std::uint64_t bytes);

    void on_metric(const MetricSample& m) override;
    void on_identity(const AdapterIdentity& a) override;
    void on_epoch_boundary(const SessionEpochBoundary&) override;

    std::uint64_t reports_emitted() const { return reports_emitted_; }

private:
    EventBus* bus_;
    std::unordered_map<std::string, std::uint64_t> dedicated_vram_;
    std::uint64_t reports_emitted_ = 0;

    bool check_48gb_pattern(const MetricSample& m);
    bool check_nan_inf(const MetricSample& m);
    bool check_impossible_heap_budget(const MetricSample& m);

    // v1.5: physically-impossible-value rules. Each preserves the raw MetricSample
    // (already written to JSONL by the upstream sink); only an additional
    // DisagreementReport is emitted. Never silently clamp.
    bool check_impossible_voltage(const MetricSample& m);
    bool check_impossible_frequency(const MetricSample& m);
    bool check_impossible_temperature(const MetricSample& m);
    bool check_impossible_percent(const MetricSample& m);
    bool check_impossible_bandwidth(const MetricSample& m);

    // previously_reporting_source_went_silent — narrow scope, IGCL only.
    // A source that previously emitted samples for (adapter, semantic_domain) stops
    // emitting for >= 3 ticks. One report per silence episode; reset on epoch boundary.
    struct TrackState {
        std::string adapter_id;
        Source source = Source::Unknown;
        SemanticDomain domain = SemanticDomain::Identity;
        std::uint64_t last_seen_ts = 0;
        std::uint64_t silence_first_detected_ts = 0;
        bool silent = false;
    };
    std::unordered_map<std::string, TrackState> source_tracking_;

    void update_source_tracking(const MetricSample& m);
    void check_for_silence(std::uint64_t now_ts, std::uint32_t session_epoch);
    void emit_source_silent(const TrackState& st, std::uint64_t now_ts, std::uint32_t epoch);
    void emit_source_resumed(const TrackState& st, std::uint64_t resumed_ts, std::uint32_t epoch);

    void emit_report(const std::string& rule,
                     const std::string& adapter_id,
                     const std::string& explanation,
                     const MetricSample& m);
};

}
