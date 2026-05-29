#include "schema/delta_filter.h"
#include "schema/enums.h"

namespace b70 {

DeltaFilter::DeltaFilter() = default;

std::string DeltaFilter::key_of(const MetricSample& m) {
    std::string k;
    k.reserve(m.metric_name.size() + m.adapter_id.size() + 2);
    k.append(m.adapter_id);
    k.push_back('|');
    k.append(m.metric_name);
    return k;
}

static bool same_value(const MetricValue& a, const MetricValue& b) {
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<ValueMissing>(a)) return true;
    if (std::holds_alternative<std::uint64_t>(a))
        return std::get<std::uint64_t>(a) == std::get<std::uint64_t>(b);
    if (std::holds_alternative<std::int64_t>(a))
        return std::get<std::int64_t>(a) == std::get<std::int64_t>(b);
    return std::get<double>(a) == std::get<double>(b);
}

bool DeltaFilter::should_emit(const MetricSample& m, std::uint64_t /*now_qpc_ns*/) {
    if (!enabled_) return true;
    if (m.flags & flags::FullSnapshot) return true;

    auto& slot = last_[key_of(m)];
    if (!slot.initialized) {
        slot.value = m.value;
        slot.flags = m.flags;
        slot.confidence = static_cast<std::uint8_t>(m.confidence);
        slot.initialized = true;
        return true;
    }
    const bool same =
        same_value(slot.value, m.value) &&
        slot.flags == m.flags &&
        slot.confidence == static_cast<std::uint8_t>(m.confidence);

    if (same) return false;

    slot.value = m.value;
    slot.flags = m.flags;
    slot.confidence = static_cast<std::uint8_t>(m.confidence);
    return true;
}

void DeltaFilter::clear() {
    last_.clear();
    last_heartbeat_ns_ = 0;
}

}
