#include "collectors/igcl_power_telemetry.h"

#include "schema/enums.h"
#include "schema/events.h"
#include "schema/metric_sample.h"

#include <windows.h>

#include "igcl_api.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace b70 {

namespace {

using PFN_ctlInit                = ctl_result_t (CTL_APICALL*)(ctl_init_args_t*, ctl_api_handle_t*);
using PFN_ctlClose               = ctl_result_t (CTL_APICALL*)(ctl_api_handle_t);
using PFN_ctlEnumerateDevices    = ctl_result_t (CTL_APICALL*)(ctl_api_handle_t, uint32_t*, ctl_device_adapter_handle_t*);
using PFN_ctlGetDeviceProperties = ctl_result_t (CTL_APICALL*)(ctl_device_adapter_handle_t, ctl_device_adapter_properties_t*);
using PFN_ctlPowerTelemetryGet   = ctl_result_t (CTL_APICALL*)(ctl_device_adapter_handle_t, ctl_power_telemetry_t*);

double telemetry_value_as_double(const ctl_oc_telemetry_item_t& it) {
    const auto& v = it.value;
    switch (it.type) {
        case CTL_DATA_TYPE_INT8:   return static_cast<double>(v.data8);
        case CTL_DATA_TYPE_UINT8:  return static_cast<double>(v.datau8);
        case CTL_DATA_TYPE_INT16:  return static_cast<double>(v.data16);
        case CTL_DATA_TYPE_UINT16: return static_cast<double>(v.datau16);
        case CTL_DATA_TYPE_INT32:  return static_cast<double>(v.data32);
        case CTL_DATA_TYPE_UINT32: return static_cast<double>(v.datau32);
        case CTL_DATA_TYPE_INT64:  return static_cast<double>(v.data64);
        case CTL_DATA_TYPE_UINT64: return static_cast<double>(v.datau64);
        case CTL_DATA_TYPE_FLOAT:  return static_cast<double>(v.datafloat);
        case CTL_DATA_TYPE_DOUBLE: return v.datadouble;
        default:                   return 0.0;
    }
}

// Normalize an IGCL telemetry item to our canonical (Unit, value) pair. Returns true if usable.
bool normalize_item(const ctl_oc_telemetry_item_t& it, Unit& out_unit, double& out_value) {
    if (!it.bSupported) return false;
    const double raw = telemetry_value_as_double(it);
    switch (it.units) {
        case CTL_UNITS_FREQUENCY_MHZ:        out_unit = Unit::Hertz;         out_value = raw * 1e6;   return true;
        case CTL_UNITS_OPERATIONS_GTS:       out_unit = Unit::Hertz;         out_value = raw * 1e9;   return true;
        case CTL_UNITS_OPERATIONS_MTS:       out_unit = Unit::Hertz;         out_value = raw * 1e6;   return true;
        case CTL_UNITS_VOLTAGE_VOLTS:        out_unit = Unit::Volts;         out_value = raw;         return true;
        case CTL_UNITS_VOLTAGE_MILLIVOLTS:   out_unit = Unit::Volts;         out_value = raw * 1e-3;  return true;
        case CTL_UNITS_POWER_WATTS:          out_unit = Unit::Watts;         out_value = raw;         return true;
        case CTL_UNITS_POWER_MILLIWATTS:     out_unit = Unit::Watts;         out_value = raw * 1e-3;  return true;
        case CTL_UNITS_TEMPERATURE_CELSIUS:  out_unit = Unit::Celsius;       out_value = raw;         return true;
        case CTL_UNITS_ENERGY_JOULES:        out_unit = Unit::Dimensionless; out_value = raw;         return true;
        case CTL_UNITS_TIME_SECONDS:         out_unit = Unit::Nanoseconds;   out_value = raw * 1e9;   return true;
        case CTL_UNITS_MEMORY_BYTES:         out_unit = Unit::Bytes;         out_value = raw;         return true;
        case CTL_UNITS_ANGULAR_SPEED_RPM:    out_unit = Unit::Rpm;           out_value = raw;         return true;
        case CTL_UNITS_PERCENT:              out_unit = Unit::Percent;       out_value = raw;         return true;
        case CTL_UNITS_MEM_SPEED_GBPS:       out_unit = Unit::BytesPerSecond;out_value = raw * 1e9;   return true;
        case CTL_UNITS_BANDWIDTH_MBPS:       out_unit = Unit::BytesPerSecond;out_value = raw * 1e6;   return true;
        default:                             out_unit = Unit::Dimensionless; out_value = raw;         return true;
    }
}

}

struct IgclPowerTelemetryCollector::Impl {
    HMODULE dll = nullptr;
    PFN_ctlInit                pInit  = nullptr;
    PFN_ctlClose               pClose = nullptr;
    PFN_ctlEnumerateDevices    pEnum  = nullptr;
    PFN_ctlGetDeviceProperties pProps = nullptr;
    PFN_ctlPowerTelemetryGet   pTel   = nullptr;
    ctl_api_handle_t           api    = nullptr;

    struct Bound {
        std::string adapter_id;
        std::uint64_t luid_raw = 0;
        ctl_device_adapter_handle_t handle = nullptr;
    };
    std::vector<Bound> bound;
    std::string init_error;
};

IgclPowerTelemetryCollector::IgclPowerTelemetryCollector() : impl_(new Impl) {}

IgclPowerTelemetryCollector::~IgclPowerTelemetryCollector() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

CollectorSideEffects IgclPowerTelemetryCollector::declared_side_effects() const {
    CollectorSideEffects s;
    s.app_passive = true;
    s.may_trigger_driver_init = true;
    s.may_wake_idle_adapter = true;     // documented IGCL behavior; counted toward DriverPassive
    s.intrusiveness = Intrusiveness::DriverPassive;
    return s;
}

bool IgclPowerTelemetryCollector::init(EventBus& bus,
                                       const std::vector<AdapterIdentity>& adapters) {
    auto& I = *impl_;
    I.dll = LoadLibraryW(L"ControlLib.dll");
    if (!I.dll) {
        I.init_error = "ControlLib.dll not loadable (Intel driver missing or WDAC blocked)";
        return false;
    }
    I.pInit  = reinterpret_cast<PFN_ctlInit>(GetProcAddress(I.dll, "ctlInit"));
    I.pClose = reinterpret_cast<PFN_ctlClose>(GetProcAddress(I.dll, "ctlClose"));
    I.pEnum  = reinterpret_cast<PFN_ctlEnumerateDevices>(GetProcAddress(I.dll, "ctlEnumerateDevices"));
    I.pProps = reinterpret_cast<PFN_ctlGetDeviceProperties>(GetProcAddress(I.dll, "ctlGetDeviceProperties"));
    I.pTel   = reinterpret_cast<PFN_ctlPowerTelemetryGet>(GetProcAddress(I.dll, "ctlPowerTelemetryGet"));
    if (!I.pInit || !I.pClose || !I.pEnum || !I.pProps || !I.pTel) {
        I.init_error = "ControlLib.dll missing required ctl* exports";
        return false;
    }

    ctl_init_args_t args{};
    args.Size = sizeof(args);
    args.Version = 0;
    args.AppVersion = CTL_IMPL_VERSION;
    args.flags = 0;
    args.SupportedVersion = 0;
    if (I.pInit(&args, &I.api) != CTL_RESULT_SUCCESS || !I.api) {
        I.init_error = "ctlInit failed";
        return false;
    }

    uint32_t dev_count = 0;
    I.pEnum(I.api, &dev_count, nullptr);
    if (dev_count == 0) {
        I.init_error = "ctlEnumerateDevices reports no devices";
        return false;
    }
    std::vector<ctl_device_adapter_handle_t> dev_handles(dev_count, nullptr);
    if (I.pEnum(I.api, &dev_count, dev_handles.data()) != CTL_RESULT_SUCCESS) {
        I.init_error = "ctlEnumerateDevices failed";
        return false;
    }

    for (auto h : dev_handles) {
        if (!h) continue;
        LUID luid_buf{};
        ctl_device_adapter_properties_t p{};
        p.Size = sizeof(p);
        p.Version = 2;
        p.pDeviceID = &luid_buf;
        p.device_id_size = sizeof(LUID);

        if (I.pProps(h, &p) != CTL_RESULT_SUCCESS) continue;

        const std::uint64_t raw =
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid_buf.HighPart)) << 32)
            | static_cast<std::uint64_t>(luid_buf.LowPart);

        for (const auto& a : adapters) {
            if (a.luid != raw) continue;
            Impl::Bound b;
            b.adapter_id = a.adapter_id;
            b.luid_raw = raw;
            b.handle = h;
            I.bound.push_back(std::move(b));

            CollectorAuditRecord rec;
            rec.collector_name = name();
            char tmp[224];
            std::snprintf(tmp, sizeof(tmp),
                          "IGCL bound %s via LUID; pci=0x%04x:0x%04x driver_version=0x%016llx name=\"%.*s\"",
                          a.adapter_id.c_str(), p.pci_vendor_id, p.pci_device_id,
                          static_cast<unsigned long long>(p.driver_version),
                          CTL_MAX_DEVICE_NAME_LEN, p.name);
            rec.notes = tmp;
            rec.session_epoch = 0;
            bus.publish(rec);
            break;
        }
    }
    return !I.bound.empty();
}

void IgclPowerTelemetryCollector::shutdown() {
    auto& I = *impl_;
    if (I.pClose && I.api) {
        I.pClose(I.api);
        I.api = nullptr;
    }
    I.bound.clear();
    if (I.dll) { FreeLibrary(I.dll); I.dll = nullptr; }
    I.pInit = nullptr; I.pClose = nullptr; I.pEnum = nullptr; I.pProps = nullptr; I.pTel = nullptr;
}

void IgclPowerTelemetryCollector::poll(std::uint64_t now_qpc_ns,
                                       std::uint32_t session_epoch,
                                       EventBus& bus) {
    auto& I = *impl_;
    if (!I.pTel) return;

    auto emit = [&](const std::string& adapter_id, const char* metric_name,
                    SemanticDomain dom, ObservationKind obs_kind,
                    const ctl_oc_telemetry_item_t& it) {
        Unit u = Unit::Dimensionless;
        double v = 0.0;
        if (!normalize_item(it, u, v)) return;
        MetricSample m;
        m.metric_name = metric_name;
        m.adapter_id = adapter_id;
        m.session_epoch = session_epoch;
        m.semantic_domain = dom;
        m.unit = u;
        m.source = Source::IGCL_PowerTelemetry;
        m.timestamp_qpc = now_qpc_ns;
        m.poll_latency_ns = 0;
        m.sampling_window_ns = 0;
        m.observation_kind = obs_kind;
        m.correlation_method = CorrelationMethod::LUID_DirectBind;
        m.confidence = Confidence::High;
        m.value = v;
        bus.publish(m);
    };

    for (auto& b : I.bound) {
        if (!b.handle) continue;
        ctl_power_telemetry_t pt{};
        pt.Size = sizeof(pt);
        pt.Version = 0;
        if (I.pTel(b.handle, &pt) != CTL_RESULT_SUCCESS) continue;

        emit(b.adapter_id, "gpu.voltage_v",                       SemanticDomain::Power,    ObservationKind::DirectlyObserved, pt.gpuVoltage);
        emit(b.adapter_id, "gpu.frequency_hz",                    SemanticDomain::Frequency,ObservationKind::DirectlyObserved, pt.gpuCurrentClockFrequency);
        emit(b.adapter_id, "gpu.temperature_c",                   SemanticDomain::Thermal,  ObservationKind::DirectlyObserved, pt.gpuCurrentTemperature);
        emit(b.adapter_id, "gpu.energy_j_counter",                SemanticDomain::Power,    ObservationKind::DirectlyObserved, pt.gpuEnergyCounter);
        emit(b.adapter_id, "gpu.activity.global_counter",         SemanticDomain::EngineActivity, ObservationKind::DirectlyObserved, pt.globalActivityCounter);
        emit(b.adapter_id, "gpu.activity.render_compute_counter", SemanticDomain::EngineActivity, ObservationKind::DirectlyObserved, pt.renderComputeActivityCounter);
        emit(b.adapter_id, "gpu.activity.media_counter",          SemanticDomain::EngineActivity, ObservationKind::DirectlyObserved, pt.mediaActivityCounter);

        emit(b.adapter_id, "vram.voltage_v",                  SemanticDomain::Power,     ObservationKind::DirectlyObserved, pt.vramVoltage);
        emit(b.adapter_id, "vram.frequency_hz",               SemanticDomain::Frequency, ObservationKind::DirectlyObserved, pt.vramCurrentClockFrequency);
        emit(b.adapter_id, "vram.frequency_effective_hz",     SemanticDomain::Frequency, ObservationKind::DirectlyObserved, pt.vramCurrentEffectiveFrequency);
        emit(b.adapter_id, "vram.temperature_c",              SemanticDomain::Thermal,   ObservationKind::DirectlyObserved, pt.vramCurrentTemperature);
        emit(b.adapter_id, "vram.energy_j_counter",           SemanticDomain::Power,     ObservationKind::DirectlyObserved, pt.vramEnergyCounter);
        emit(b.adapter_id, "vram.read_bandwidth_counter",     SemanticDomain::Memory,    ObservationKind::DirectlyObserved, pt.vramReadBandwidthCounter);
        emit(b.adapter_id, "vram.write_bandwidth_counter",    SemanticDomain::Memory,    ObservationKind::DirectlyObserved, pt.vramWriteBandwidthCounter);

        emit(b.adapter_id, "card.energy_j_counter", SemanticDomain::Power, ObservationKind::DirectlyObserved, pt.totalCardEnergyCounter);

        for (uint32_t fi = 0; fi < CTL_FAN_COUNT; ++fi) {
            if (!pt.fanSpeed[fi].bSupported) continue;
            char nm[48];
            std::snprintf(nm, sizeof(nm), "card.fan%u.speed", fi);
            emit(b.adapter_id, nm, SemanticDomain::Thermal, ObservationKind::DirectlyObserved, pt.fanSpeed[fi]);
        }
    }
}

}
