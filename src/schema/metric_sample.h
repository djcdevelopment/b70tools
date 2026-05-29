#pragma once

#include "schema/enums.h"
#include <cstdint>
#include <string>
#include <variant>

namespace b70 {

struct ValueMissing {};

using MetricValue = std::variant<ValueMissing, std::uint64_t, std::int64_t, double>;

struct MetricSample {
    std::string metric_name;
    std::string adapter_id;
    std::uint32_t session_epoch = 0;
    SemanticDomain semantic_domain = SemanticDomain::Identity;

    MetricValue value;
    Unit unit = Unit::Dimensionless;

    Source source = Source::Unknown;
    std::string source_detail;

    std::uint64_t timestamp_qpc = 0;
    std::uint64_t poll_latency_ns = 0;
    std::uint64_t sampling_window_ns = 0;

    ObservationKind observation_kind = ObservationKind::DirectlyObserved;
    CorrelationMethod correlation_method = CorrelationMethod::Unbound;
    Confidence confidence = Confidence::Medium;
    std::uint32_t flags = 0;
};

}
