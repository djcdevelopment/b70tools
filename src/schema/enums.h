#pragma once

#include <cstdint>
#include <string_view>

namespace b70 {

enum class SemanticDomain : std::uint8_t {
    Identity, Memory, Power, Thermal, Frequency,
    EngineActivity, Process, Driver, Arbitration, PCIe,
};

enum class Source : std::uint8_t {
    Unknown,
    DXGI,
    Vulkan_Enumeration,
    Vulkan_MemoryBudget,
    DXGI_VideoMemoryInfo,
    D3DKMT_PerfData,
    D3DKMT_Statistics,
    IGCL_PowerTelemetry,
    PDH_GpuEngine,
    PDH_AdapterMemory,
    SetupAPI,
    Registry,
    SMBIOS,
    FakeCollector,
    ReplayReader,
    Arbitrator,
};

enum class ObservationKind : std::uint8_t {
    DirectlyObserved,
    DerivedFromDelta,
    Inferred,
    Estimated,
    Reported_Untrusted,
};

enum class CorrelationMethod : std::uint8_t {
    LUID_DirectBind,
    BDF_Match,
    ProcessID_Filter,
    DriverHandle,
    Unbound,
};

enum class Confidence : std::uint8_t {
    High, Medium, Low, Disagreed,
};

enum class Unit : std::uint8_t {
    Dimensionless, Bytes, Hertz, Watts, Celsius,
    Percent, Volts, Ticks, Nanoseconds,
    BytesPerSecond, Rpm,
};

enum class AdapterState : std::uint8_t {
    Unknown, Idle, Awake, ActiveCompute,
    SuspectedComputeHidden, PostTDR, Lost, Reenumerating,
};

enum class EventKind : std::uint8_t {
    MetricSample,
    AdapterIdentity,
    AdapterStateTransition,
    DisagreementReport,
    SessionEpochBoundary,
    DriverRuntimeFingerprint,
    CollectorAuditRecord,
};

namespace flags {
    constexpr std::uint32_t Stale                  = 1u << 0;
    constexpr std::uint32_t PostTDR                = 1u << 1;
    constexpr std::uint32_t FirstSampleAfterEnum   = 1u << 2;
    constexpr std::uint32_t BudgetExceeded         = 1u << 3;
    constexpr std::uint32_t SourceEmittedNaN       = 1u << 4;
    constexpr std::uint32_t SourceEmittedInf       = 1u << 5;
    constexpr std::uint32_t PDH_CStatus_NonZero    = 1u << 6;
    constexpr std::uint32_t TimeoutFallback        = 1u << 7;
    constexpr std::uint32_t FullSnapshot           = 1u << 8;
    constexpr std::uint32_t SyntheticInjection     = 1u << 9;
}

std::string_view to_string(SemanticDomain);
std::string_view to_string(Source);
std::string_view to_string(ObservationKind);
std::string_view to_string(CorrelationMethod);
std::string_view to_string(Confidence);
std::string_view to_string(Unit);
std::string_view to_string(AdapterState);
std::string_view to_string(EventKind);

}
