#pragma once

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/json_emit.h"
#include "schema/metric_sample.h"

#include <string>
#include <string_view>

namespace b70::compact {

inline constexpr std::string_view kKindMetric         = "ms";
inline constexpr std::string_view kKindIdentity       = "ai";
inline constexpr std::string_view kKindStateTrans     = "ast";
inline constexpr std::string_view kKindDisagreement   = "dr";
inline constexpr std::string_view kKindEpoch          = "seb";
inline constexpr std::string_view kKindFingerprint    = "drf";
inline constexpr std::string_view kKindAudit          = "car";

void encode_metric(json_emit::LineBuilder& out, const MetricSample& m,
                   bool& out_nan_seen, bool& out_inf_seen);
void encode_identity(json_emit::LineBuilder& out, const AdapterIdentity& a);
void encode_state_transition(json_emit::LineBuilder& out, const AdapterStateTransition& t);
void encode_disagreement(json_emit::LineBuilder& out, const DisagreementReport& d);
void encode_epoch_boundary(json_emit::LineBuilder& out, const SessionEpochBoundary& e);
void encode_fingerprint(json_emit::LineBuilder& out, const DriverRuntimeFingerprint& f);
void encode_audit(json_emit::LineBuilder& out, const CollectorAuditRecord& c);

std::string schema_sidecar_json();

}
