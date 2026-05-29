#include "schema/compact_format.h"

namespace b70::compact {

namespace {

void emit_value(json_emit::LineBuilder& out, const MetricValue& v,
                bool& out_nan_seen, bool& out_inf_seen) {
    if (std::holds_alternative<ValueMissing>(v)) {
        out.value_null();
    } else if (std::holds_alternative<std::uint64_t>(v)) {
        out.value_u64(std::get<std::uint64_t>(v));
    } else if (std::holds_alternative<std::int64_t>(v)) {
        out.value_i64(std::get<std::int64_t>(v));
    } else {
        out.value_f64_or_missing(std::get<double>(v), out_nan_seen, out_inf_seen);
    }
}

}

void encode_metric(json_emit::LineBuilder& out, const MetricSample& m,
                   bool& out_nan_seen, bool& out_inf_seen) {
    out.begin_object();
    out.key("k"); out.value_string(kKindMetric);
    out.key("n"); out.value_string(m.metric_name);
    out.key("a"); out.value_string(m.adapter_id);
    out.key("e"); out.value_u64(m.session_epoch);
    out.key("d"); out.value_string(to_string(m.semantic_domain));
    out.key("v"); emit_value(out, m.value, out_nan_seen, out_inf_seen);
    out.key("u"); out.value_string(to_string(m.unit));
    out.key("s"); out.value_string(to_string(m.source));
    if (!m.source_detail.empty()) { out.key("p"); out.value_string(m.source_detail); }
    out.key("t"); out.value_u64(m.timestamp_qpc);
    out.key("l"); out.value_u64(m.poll_latency_ns);
    out.key("w"); out.value_u64(m.sampling_window_ns);
    out.key("o"); out.value_string(to_string(m.observation_kind));
    out.key("c"); out.value_string(to_string(m.correlation_method));
    out.key("f"); out.value_string(to_string(m.confidence));
    out.key("g"); out.value_u64(m.flags);
    out.end_object();
    out.newline();
}

void encode_identity(json_emit::LineBuilder& out, const AdapterIdentity& a) {
    out.begin_object();
    out.key("k"); out.value_string(kKindIdentity);
    out.key("a"); out.value_string(a.adapter_id);
    out.key("luid"); out.value_u64(a.luid);
    out.key("desc"); out.value_string(a.description);
    out.key("dvm"); out.value_u64(a.dedicated_video_memory);
    out.key("ssm"); out.value_u64(a.shared_system_memory);
    out.key("bdf"); out.value_string(a.pci_bdf);
    out.key("druu"); out.value_string(a.driver_uuid);
    out.key("e"); out.value_u64(a.session_epoch);
    out.key("t"); out.value_u64(a.timestamp_qpc);
    out.key("bind"); out.begin_array();
    for (const auto& b : a.bindings) { out.value_string(b); }
    out.end_array();
    out.end_object();
    out.newline();
}

void encode_state_transition(json_emit::LineBuilder& out, const AdapterStateTransition& t) {
    out.begin_object();
    out.key("k"); out.value_string(kKindStateTrans);
    out.key("a"); out.value_string(t.adapter_id);
    out.key("from"); out.value_string(to_string(t.from));
    out.key("to"); out.value_string(to_string(t.to));
    out.key("r"); out.value_string(t.reason);
    out.key("e"); out.value_u64(t.session_epoch);
    out.key("t"); out.value_u64(t.timestamp_qpc);
    out.end_object();
    out.newline();
}

void encode_disagreement(json_emit::LineBuilder& out, const DisagreementReport& d) {
    out.begin_object();
    out.key("k"); out.value_string(kKindDisagreement);
    out.key("rule"); out.value_string(d.rule_name);
    out.key("a"); out.value_string(d.adapter_id);
    out.key("expl"); out.value_string(d.explanation);
    out.key("cf"); out.value_string(to_string(d.resulting_confidence));
    out.key("srcs"); out.begin_array();
    for (const auto& s : d.involved_sources) { out.value_string(s); }
    out.end_array();
    out.key("e"); out.value_u64(d.session_epoch);
    out.key("t"); out.value_u64(d.timestamp_qpc);
    out.end_object();
    out.newline();
}

void encode_epoch_boundary(json_emit::LineBuilder& out, const SessionEpochBoundary& e) {
    out.begin_object();
    out.key("k"); out.value_string(kKindEpoch);
    out.key("prev"); out.value_u64(e.previous_epoch);
    out.key("new"); out.value_u64(e.new_epoch);
    out.key("cause"); out.value_string(e.cause);
    out.key("t"); out.value_u64(e.timestamp_qpc);
    out.end_object();
    out.newline();
}

void encode_fingerprint(json_emit::LineBuilder& out, const DriverRuntimeFingerprint& f) {
    out.begin_object();
    out.key("k"); out.value_string(kKindFingerprint);
    out.key("win"); out.value_string(f.windows_build);
    out.key("bios"); out.value_string(f.bios_version);
    out.key("biosd"); out.value_string(f.bios_date);
    out.key("idrv"); out.value_string(f.intel_driver_version);
    out.key("vkrt"); out.value_string(f.vulkan_runtime_version);
    out.key("hags"); out.value_bool(f.hags_enabled);
    out.key("wdac"); out.value_bool(f.wdac_enforced);
    out.key("a4g"); out.value_bool(f.above_4g_decoding);
    out.key("e"); out.value_u64(f.session_epoch);
    out.key("t"); out.value_u64(f.timestamp_qpc);
    out.key("notes"); out.begin_array();
    for (const auto& n : f.per_adapter_notes) { out.value_string(n); }
    out.end_array();
    out.end_object();
    out.newline();
}

void encode_audit(json_emit::LineBuilder& out, const CollectorAuditRecord& c) {
    out.begin_object();
    out.key("k"); out.value_string(kKindAudit);
    out.key("name"); out.value_string(c.collector_name);
    out.key("tb"); out.value_u64(c.threads_before);
    out.key("ta"); out.value_u64(c.threads_after);
    out.key("mods"); out.begin_array();
    for (const auto& m : c.modules_loaded) { out.value_string(m); }
    out.end_array();
    out.key("iw"); out.value_u64(c.init_wall_ns);
    out.key("rss"); out.value_i64(c.rss_delta_bytes);
    out.key("tp"); out.value_bool(c.declared_truly_passive);
    out.key("dp"); out.value_bool(c.declared_driver_passive);
    out.key("undec"); out.value_bool(c.observed_undeclared_side_effects);
    out.key("note"); out.value_string(c.notes);
    out.key("e"); out.value_u64(c.session_epoch);
    out.key("t"); out.value_u64(c.timestamp_qpc);
    out.end_object();
    out.newline();
}

std::string schema_sidecar_json() {
    return R"({
  "format": "b70tools-jsonl-compact-v1",
  "event_kinds": {
    "ms":  "MetricSample",
    "ai":  "AdapterIdentity",
    "ast": "AdapterStateTransition",
    "dr":  "DisagreementReport",
    "seb": "SessionEpochBoundary",
    "drf": "DriverRuntimeFingerprint",
    "car": "CollectorAuditRecord"
  },
  "collector_audit_fields": {
    "tp":    "declared_truly_passive",
    "dp":    "declared_driver_passive",
    "tb":    "threads_before",
    "ta":    "threads_after",
    "mods":  "modules_loaded",
    "iw":    "init_wall_ns",
    "rss":   "rss_delta_bytes",
    "undec": "observed_undeclared_side_effects",
    "note":  "notes"
  },
  "metric_sample_fields": {
    "k":  "event_kind",
    "n":  "metric_name",
    "a":  "adapter_id",
    "e":  "session_epoch",
    "d":  "semantic_domain",
    "v":  "value",
    "u":  "unit",
    "s":  "source",
    "p":  "source_detail",
    "t":  "timestamp_qpc",
    "l":  "poll_latency_ns",
    "w":  "sampling_window_ns",
    "o":  "observation_kind",
    "c":  "correlation_method",
    "f":  "confidence",
    "g":  "flags"
  }
}
)";
}

}
