#include "schema/enums.h"

namespace b70 {

std::string_view to_string(SemanticDomain d) {
    switch (d) {
        case SemanticDomain::Identity:        return "Identity";
        case SemanticDomain::Memory:          return "Memory";
        case SemanticDomain::Power:           return "Power";
        case SemanticDomain::Thermal:         return "Thermal";
        case SemanticDomain::Frequency:       return "Frequency";
        case SemanticDomain::EngineActivity:  return "EngineActivity";
        case SemanticDomain::Process:         return "Process";
        case SemanticDomain::Driver:          return "Driver";
        case SemanticDomain::Arbitration:     return "Arbitration";
        case SemanticDomain::PCIe:            return "PCIe";
    }
    return "Unknown";
}

std::string_view to_string(Source s) {
    switch (s) {
        case Source::Unknown:               return "Unknown";
        case Source::DXGI:                  return "DXGI";
        case Source::Vulkan_Enumeration:    return "Vulkan_Enumeration";
        case Source::Vulkan_MemoryBudget:   return "Vulkan_MemoryBudget";
        case Source::DXGI_VideoMemoryInfo:  return "DXGI_VideoMemoryInfo";
        case Source::D3DKMT_PerfData:       return "D3DKMT_PerfData";
        case Source::D3DKMT_Statistics:     return "D3DKMT_Statistics";
        case Source::IGCL_PowerTelemetry:   return "IGCL_PowerTelemetry";
        case Source::PDH_GpuEngine:         return "PDH_GpuEngine";
        case Source::PDH_AdapterMemory:     return "PDH_AdapterMemory";
        case Source::SetupAPI:              return "SetupAPI";
        case Source::Registry:              return "Registry";
        case Source::SMBIOS:                return "SMBIOS";
        case Source::FakeCollector:         return "FakeCollector";
        case Source::ReplayReader:          return "ReplayReader";
        case Source::Arbitrator:            return "Arbitrator";
    }
    return "Unknown";
}

std::string_view to_string(ObservationKind k) {
    switch (k) {
        case ObservationKind::DirectlyObserved:     return "DirectlyObserved";
        case ObservationKind::DerivedFromDelta:     return "DerivedFromDelta";
        case ObservationKind::Inferred:             return "Inferred";
        case ObservationKind::Estimated:            return "Estimated";
        case ObservationKind::Reported_Untrusted:   return "Reported_Untrusted";
    }
    return "Unknown";
}

std::string_view to_string(CorrelationMethod c) {
    switch (c) {
        case CorrelationMethod::LUID_DirectBind:    return "LUID_DirectBind";
        case CorrelationMethod::BDF_Match:          return "BDF_Match";
        case CorrelationMethod::ProcessID_Filter:   return "ProcessID_Filter";
        case CorrelationMethod::DriverHandle:       return "DriverHandle";
        case CorrelationMethod::Unbound:            return "Unbound";
    }
    return "Unknown";
}

std::string_view to_string(Confidence c) {
    switch (c) {
        case Confidence::High:       return "High";
        case Confidence::Medium:     return "Medium";
        case Confidence::Low:        return "Low";
        case Confidence::Disagreed:  return "Disagreed";
    }
    return "Unknown";
}

std::string_view to_string(Unit u) {
    switch (u) {
        case Unit::Dimensionless: return "Dimensionless";
        case Unit::Bytes:         return "Bytes";
        case Unit::Hertz:         return "Hertz";
        case Unit::Watts:         return "Watts";
        case Unit::Celsius:       return "Celsius";
        case Unit::Percent:       return "Percent";
        case Unit::Volts:         return "Volts";
        case Unit::Ticks:         return "Ticks";
        case Unit::Nanoseconds:   return "Nanoseconds";
        case Unit::BytesPerSecond:return "BytesPerSecond";
        case Unit::Rpm:           return "Rpm";
    }
    return "Unknown";
}

std::string_view to_string(AdapterState s) {
    switch (s) {
        case AdapterState::Unknown:                 return "Unknown";
        case AdapterState::Idle:                    return "Idle";
        case AdapterState::Awake:                   return "Awake";
        case AdapterState::ActiveCompute:           return "ActiveCompute";
        case AdapterState::SuspectedComputeHidden:  return "SuspectedComputeHidden";
        case AdapterState::PostTDR:                 return "PostTDR";
        case AdapterState::Lost:                    return "Lost";
        case AdapterState::Reenumerating:           return "Reenumerating";
    }
    return "Unknown";
}

std::string_view to_string(EventKind k) {
    switch (k) {
        case EventKind::MetricSample:               return "MetricSample";
        case EventKind::AdapterIdentity:            return "AdapterIdentity";
        case EventKind::AdapterStateTransition:     return "AdapterStateTransition";
        case EventKind::DisagreementReport:         return "DisagreementReport";
        case EventKind::SessionEpochBoundary:       return "SessionEpochBoundary";
        case EventKind::DriverRuntimeFingerprint:   return "DriverRuntimeFingerprint";
        case EventKind::CollectorAuditRecord:       return "CollectorAuditRecord";
    }
    return "Unknown";
}

}
