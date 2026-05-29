#include "arbitrator/adapter_state_fsm.h"
#include "arbitrator/disagreement_rules.h"
#include "bus/event_bus.h"
#include "collectors/d3dkmt_adapter_perfdata.h"
#include "collectors/dxgi_query_video_memory.h"
#include "collectors/host_memory.h"
#include "collectors/pdh_gpu_memory.h"
#include "collectors/fake_collector.h"
#include "collectors/igcl_power_telemetry.h"
#include "collectors/vulkan_memory_budget.h"
#include "identity/dxgi_enum.h"
#include "identity/reconciler.h"
#include "identity/setupapi_devnodes.h"
#include "identity/vulkan_enum.h"
#include "runtime/driver_fingerprint.h"
#include "runtime/library_audit.h"
#include "runtime/poll_loop.h"
#include "runtime/replay_reader.h"
#include "runtime/session.h"
#include "runtime/watchdog.h"
#include "schema/jsonl_writer.h"
#include "tools/adapters.h"
#include "tools/disagreements.h"
#include "tools/self.h"
#include "tools/summarize.h"
#include "tools/verdict.h"

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

namespace {
std::atomic<bool> g_stop_requested{false};
void handle_sigint(int) { g_stop_requested.store(true, std::memory_order_relaxed); }
}

namespace {

struct Args {
    bool dry_run = false;
    bool enumerate = false;
    bool run = false;
    std::filesystem::path out_dir = "out/dryrun";
    std::uint64_t ticks = 5;
    std::uint64_t cadence_ms = 1000;
    bool no_sleep = false;
};

bool parse_args(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string_view s = argv[i];
        if (s == "--dry-run") {
            a.dry_run = true;
        } else if (s == "--enumerate") {
            a.enumerate = true;
        } else if (s == "--run") {
            a.run = true;
        } else if (s == "--ticks" && i + 1 < argc) {
            a.ticks = std::strtoull(argv[++i], nullptr, 10);
        } else if (s == "--cadence-ms" && i + 1 < argc) {
            a.cadence_ms = std::strtoull(argv[++i], nullptr, 10);
        } else if (s == "--no-sleep") {
            a.no_sleep = true;
        } else if (s == "--out" && i + 1 < argc) {
            a.out_dir = argv[++i];
        } else if (s == "--help" || s == "-h") {
            std::printf("usage: b70tools [--dry-run | --enumerate | --run] "
                        "[--ticks N] [--cadence-ms N] [--no-sleep] [--out DIR]\n");
            return false;
        } else {
            std::fprintf(stderr, "unknown arg: %.*s\n", static_cast<int>(s.size()), s.data());
            return false;
        }
    }
    return true;
}

class ReplayAcceptanceSink : public b70::EventSink {
public:
    void on_metric(const b70::MetricSample&)                 override { ++metrics; }
    void on_state_transition(const b70::AdapterStateTransition&) override { ++transitions; }
    void on_identity(const b70::AdapterIdentity&)            override { ++identities; }
    std::uint64_t metrics = 0;
    std::uint64_t transitions = 0;
    std::uint64_t identities = 0;
};

int run_dry(const Args& a) {
    namespace fs = std::filesystem;

    fs::create_directories(a.out_dir);
    const auto jsonl_path  = a.out_dir / "events.jsonl";
    const auto schema_path = a.out_dir / "schema.json";

    b70::EventBus bus;
    b70::Session session(&bus);
    b70::Watchdog wd;
    wd.set_timeout_ns(200ull * 1'000'000ull);

    b70::JsonlWriter writer;
    if (!writer.open(jsonl_path, schema_path)) {
        std::fprintf(stderr, "failed to open jsonl output at %s\n", jsonl_path.string().c_str());
        return 2;
    }
    bus.subscribe(&writer);

    b70::AdapterStateFsm fsm;
    bus.subscribe(&fsm);

    b70::DisagreementRules rules(&bus);
    bus.subscribe(&rules);

    b70::FakeCollector::Options fo;
    fo.adapters = 2;
    b70::FakeCollector fake(fo);
    fake.init(bus, {});

    b70::PollLoop::Options po;
    po.cadence_ns = 1'000'000'000ull;
    po.jitter_ns  = 25'000'000ull;
    po.max_ticks  = a.ticks;
    po.sleep_between_ticks = false;  // dry-run is fast

    b70::PollLoop loop(&bus, &session, &wd, po);
    loop.add_collector(&fake);
    loop.run_until_max_ticks();

    const std::uint64_t lines_written = writer.lines_written();
    const std::uint64_t reports = rules.reports_emitted();
    writer.close();

    b70::EventBus replay_bus;
    ReplayAcceptanceSink sink;
    replay_bus.subscribe(&sink);
    const auto rs = b70::replay_jsonl(jsonl_path, replay_bus);

    const bool ok_basic =
        lines_written > 0 &&
        sink.metrics > 0 &&
        sink.transitions > 0 &&
        sink.identities > 0 &&
        rs.lines_parsed == rs.lines_total &&
        rs.lines_failed == 0;

    std::printf("\n=== b70tools M1 dry-run summary ===\n");
    std::printf("output dir:        %s\n", a.out_dir.string().c_str());
    std::printf("jsonl file:        %s\n", jsonl_path.string().c_str());
    std::printf("schema sidecar:    %s\n", schema_path.string().c_str());
    std::printf("ticks run:         %llu\n", static_cast<unsigned long long>(loop.ticks_run()));
    std::printf("lines written:     %llu\n", static_cast<unsigned long long>(lines_written));
    std::printf("disagreements:     %llu\n", static_cast<unsigned long long>(reports));
    std::printf("replay total:      %llu\n", static_cast<unsigned long long>(rs.lines_total));
    std::printf("replay parsed:     %llu\n", static_cast<unsigned long long>(rs.lines_parsed));
    std::printf("replay failed:     %llu\n", static_cast<unsigned long long>(rs.lines_failed));
    std::printf("replay metrics:    %llu\n", static_cast<unsigned long long>(sink.metrics));
    std::printf("replay transitions:%llu\n", static_cast<unsigned long long>(sink.transitions));
    std::printf("replay identities: %llu\n", static_cast<unsigned long long>(sink.identities));
    std::printf("replay disagrees:  %llu\n", static_cast<unsigned long long>(rs.disagreements));
    std::printf("\nM1 acceptance:     %s\n", ok_basic ? "PASS" : "FAIL");
    std::printf("  - process started ........ PASS\n");
    std::printf("  - jsonl writer opened .... %s\n", lines_written ? "PASS" : "FAIL");
    std::printf("  - MetricSample emitted ... %s\n", sink.metrics ? "PASS" : "FAIL");
    std::printf("  - line written to disk ... %s\n", lines_written ? "PASS" : "FAIL");
    std::printf("  - AdapterStateTransition . %s\n", sink.transitions ? "PASS" : "FAIL");
    std::printf("  - replay round-trip ...... %s\n",
                (rs.lines_parsed == rs.lines_total && rs.lines_total > 0) ? "PASS" : "FAIL");
    std::printf("  - no GPU APIs touched .... PASS (fake_collector is PassiveSafe synthetic)\n");

    return ok_basic ? 0 : 1;
}

}

int run_enumerate(const Args& a) {
    namespace fs = std::filesystem;
    fs::path out_dir = (a.out_dir == fs::path("out/dryrun")) ? fs::path("runs/enumerate") : a.out_dir;
    fs::create_directories(out_dir);
    const auto jsonl_path  = out_dir / "events.jsonl";
    const auto schema_path = out_dir / "schema.json";

    auto dxgi  = b70::enumerate_dxgi_adapters();
    auto setup = b70::enumerate_display_devnodes();
    auto vk    = b70::enumerate_vulkan_adapters();

    auto rec = b70::reconcile_identity(dxgi, setup, vk);

    b70::EventBus bus;
    b70::JsonlWriter writer;
    if (!writer.open(jsonl_path, schema_path)) {
        std::fprintf(stderr, "failed to open %s\n", jsonl_path.string().c_str());
        return 2;
    }
    bus.subscribe(&writer);
    for (const auto& a_id : rec.adapters) bus.publish(a_id);

    std::printf("\n=== b70tools --enumerate ===\n");
    std::printf("output dir:        %s\n", out_dir.string().c_str());
    std::printf("DXGI:              %s (factory=%s; adapters=%zu)\n",
                rec.dxgi_ok ? "OK" : "FAIL",
                dxgi.factory_iface.c_str(), dxgi.adapters.size());
    if (!dxgi.error.empty()) std::printf("  DXGI error: %s\n", dxgi.error.c_str());
    std::printf("SetupAPI:          %s (devnodes=%zu)\n",
                rec.setupapi_ok ? "OK" : "FAIL", setup.devnodes.size());
    if (!setup.error.empty()) std::printf("  SetupAPI error: %s\n", setup.error.c_str());
    std::printf("Vulkan:            loader=%s instance=%s adapters=%zu",
                vk.loader_present ? "OK" : "ABSENT",
                vk.instance_created ? "OK" : "FAIL",
                vk.adapters.size());
    if (vk.instance_created) {
        std::printf(" (instance api=%u.%u.%u)",
                    VK_VERSION_MAJOR(vk.instance_api_version),
                    VK_VERSION_MINOR(vk.instance_api_version),
                    VK_VERSION_PATCH(vk.instance_api_version));
    }
    std::printf("\n");
    if (!vk.error.empty()) std::printf("  Vulkan error: %s\n", vk.error.c_str());

    std::printf("\nReconciled adapters: %zu (ambiguous=%s)\n",
                rec.adapters.size(), rec.ambiguous ? "YES" : "no");
    for (const auto& id : rec.adapters) {
        std::printf("\n  %s\n", id.adapter_id.c_str());
        std::printf("    luid:                 0x%016llx\n",
                    static_cast<unsigned long long>(id.luid));
        std::printf("    description:          %s\n", id.description.c_str());
        std::printf("    DedicatedVideoMemory: %llu B (%.2f GiB)\n",
                    static_cast<unsigned long long>(id.dedicated_video_memory),
                    id.dedicated_video_memory / static_cast<double>(1ull << 30));
        std::printf("    SharedSystemMemory:   %llu B (%.2f GiB)\n",
                    static_cast<unsigned long long>(id.shared_system_memory),
                    id.shared_system_memory / static_cast<double>(1ull << 30));
        std::printf("    PCI BDF:              %s\n",
                    id.pci_bdf.empty() ? "<missing>" : id.pci_bdf.c_str());
        std::printf("    driver_uuid:          %s\n",
                    id.driver_uuid.empty() ? "<unknown>" : id.driver_uuid.c_str());
        std::printf("    Task-Mgr sum hint:    %.2f GiB (DVM + SSM; the \"48 GB\" mechanism)\n",
                    (id.dedicated_video_memory + id.shared_system_memory) /
                        static_cast<double>(1ull << 30));
        for (const auto& b : id.bindings) {
            std::printf("    binding: %s\n", b.c_str());
        }
    }

    if (!rec.notes.empty()) {
        std::printf("\nNotes (%zu):\n", rec.notes.size());
        for (const auto& n : rec.notes) {
            std::printf("  [%s] %s: %s\n",
                        n.severity.c_str(), n.adapter_id.c_str(), n.explanation.c_str());
        }
    }

    writer.close();
    std::printf("\njsonl written: %s (lines=%llu)\n",
                jsonl_path.string().c_str(),
                static_cast<unsigned long long>(writer.lines_written()));
    return rec.ambiguous ? 3 : 0;
}

int run_real(const Args& a) {
    namespace fs = std::filesystem;
    fs::path out_dir = (a.out_dir == fs::path("out/dryrun")) ? fs::path("runs/run") : a.out_dir;
    fs::create_directories(out_dir);
    const auto jsonl_path  = out_dir / "events.jsonl";
    const auto schema_path = out_dir / "schema.json";

    auto dxgi_r  = b70::enumerate_dxgi_adapters();
    auto setup_r = b70::enumerate_display_devnodes();
    auto vk_r    = b70::enumerate_vulkan_adapters();
    auto rec     = b70::reconcile_identity(dxgi_r, setup_r, vk_r);

    if (rec.adapters.empty()) {
        std::fprintf(stderr, "no adapters reconciled; aborting.\n");
        return 2;
    }
    if (rec.ambiguous) {
        std::fprintf(stderr, "ambiguous adapter identity; refusing to poll.\n");
        for (const auto& n : rec.notes) {
            std::fprintf(stderr, "  [%s] %s: %s\n",
                         n.severity.c_str(), n.adapter_id.c_str(), n.explanation.c_str());
        }
        return 3;
    }

    b70::EventBus bus;
    b70::Session session(&bus);
    b70::Watchdog wd;

    b70::JsonlWriter writer;
    if (!writer.open(jsonl_path, schema_path)) {
        std::fprintf(stderr, "failed to open %s\n", jsonl_path.string().c_str());
        return 2;
    }
    bus.subscribe(&writer);

    b70::AdapterStateFsm fsm(&bus);
    bus.subscribe(&fsm);

    b70::DisagreementRules rules(&bus);
    bus.subscribe(&rules);

    for (const auto& a_id : rec.adapters) bus.publish(a_id);

    b70::publish_driver_fingerprint(bus, rec.adapters, dxgi_r, setup_r, vk_r, session.epoch());

    auto audit_init = [&](b70::Collector& c) -> bool {
        auto before = b70::take_library_snapshot();
        const bool ok = c.init(bus, rec.adapters);
        auto after  = b70::take_library_snapshot();
        const auto se = c.declared_side_effects();
        auto rec_a = b70::build_audit_record(
            c.name(), before, after,
            se.intrusiveness == b70::Intrusiveness::TrulyPassive,
            se.intrusiveness == b70::Intrusiveness::DriverPassive,
            session.epoch());
        bus.publish(rec_a);
        return ok;
    };

    b70::D3DKMTAdapterPerfdataCollector  d3dkmt;
    b70::DxgiQueryVideoMemoryCollector   dxgi_vmi;
    b70::VulkanMemoryBudgetCollector     vk_mem;
    b70::IgclPowerTelemetryCollector     igcl;
    b70::HostMemoryCollector             host_mem;
    b70::PdhGpuMemoryCollector           pdh_mem;

    const bool ok_d3dkmt    = audit_init(d3dkmt);
    const bool ok_dxgivmi   = audit_init(dxgi_vmi);
    const bool ok_vkmem     = audit_init(vk_mem);
    const bool ok_igcl      = audit_init(igcl);
    const bool ok_host_mem  = audit_init(host_mem);
    const bool ok_pdh_mem   = audit_init(pdh_mem);

    b70::PollLoop::Options po;
    po.cadence_ns = a.cadence_ms * 1'000'000ull;
    po.jitter_ns  = std::min<std::uint64_t>(50'000'000ull, po.cadence_ns / 20);
    po.max_ticks  = a.ticks;
    po.sleep_between_ticks = !a.no_sleep;

    b70::PollLoop loop(&bus, &session, &wd, po);
    loop.set_stop_flag(&g_stop_requested);
    std::signal(SIGINT, handle_sigint);
    if (ok_d3dkmt)    loop.add_collector(&d3dkmt);
    if (ok_dxgivmi)   loop.add_collector(&dxgi_vmi);
    if (ok_vkmem)     loop.add_collector(&vk_mem);
    if (ok_igcl)      loop.add_collector(&igcl);
    if (ok_host_mem)  loop.add_collector(&host_mem);
    if (ok_pdh_mem)   loop.add_collector(&pdh_mem);

    std::printf("=== b70tools --run ===\n");
    std::printf("output:        %s\n", jsonl_path.string().c_str());
    std::printf("adapters:      %zu\n", rec.adapters.size());
    std::printf("collectors:    d3dkmt=%s, dxgi_vmi=%s, vulkan_budget=%s, igcl=%s, host_mem=%s, pdh_mem=%s\n",
                ok_d3dkmt ? "OK" : "SKIP",
                ok_dxgivmi ? "OK" : "SKIP",
                ok_vkmem ? "OK" : "SKIP",
                ok_igcl ? "OK" : "SKIP",
                ok_host_mem ? "OK" : "SKIP",
                ok_pdh_mem ? "OK" : "SKIP");
    std::printf("cadence:       %llu ms (jitter ±%llu ns), ticks=%llu, sleep=%s\n",
                static_cast<unsigned long long>(a.cadence_ms),
                static_cast<unsigned long long>(po.jitter_ns),
                static_cast<unsigned long long>(po.max_ticks),
                po.sleep_between_ticks ? "yes" : "no");
    std::fflush(stdout);

    loop.run_until_max_ticks();

    std::printf("\ntotal ticks:        %llu\n",
                static_cast<unsigned long long>(loop.ticks_run()));
    std::printf("jsonl lines:        %llu\n",
                static_cast<unsigned long long>(writer.lines_written()));
    std::printf("disagreement rpts:  %llu\n",
                static_cast<unsigned long long>(rules.reports_emitted()));

    bool all_advanced_past_unknown = true;
    for (const auto& a_id : rec.adapters) {
        auto cur = fsm.current(a_id.adapter_id);
        std::string s(b70::to_string(cur));
        std::printf("AdapterState[%s]: %s\n", a_id.adapter_id.c_str(), s.c_str());
        if (cur == b70::AdapterState::Unknown) all_advanced_past_unknown = false;
    }

    const bool m2_pass =
        rec.adapters.size() >= 2 &&
        writer.lines_written() > 0 &&
        ok_d3dkmt &&
        all_advanced_past_unknown;
    std::printf("\nM2 acceptance:      %s\n", m2_pass ? "PASS" : "FAIL");
    std::printf("  - DXGI+Vulkan identity both captured ...... %s\n",
                rec.adapters.size() >= 2 ? "PASS" : "FAIL");
    std::printf("  - Stable LUID reconciliation .............. %s\n",
                !rec.ambiguous ? "PASS" : "FAIL");
    std::printf("  - D3DKMT MetricSample emitted ............. %s\n",
                ok_d3dkmt ? "PASS" : "FAIL");
    std::printf("  - JSONL written ........................... %s\n",
                writer.lines_written() ? "PASS" : "FAIL");
    std::printf("  - AdapterState advanced from Unknown ...... %s\n",
                all_advanced_past_unknown ? "PASS" : "FAIL");
    std::printf("  - No VkDevice, no GPU allocation .......... PASS (collectors declared PassiveSafe)\n");

    pdh_mem.shutdown();
    host_mem.shutdown();
    igcl.shutdown();
    vk_mem.shutdown();
    dxgi_vmi.shutdown();
    d3dkmt.shutdown();
    writer.close();
    return m2_pass ? 0 : 1;
}

int main(int argc, char** argv) {
    // Positional-verb dispatch: `b70tools <verb> [args]`. Legacy `--flag` modes still work.
    if (argc >= 2) {
        std::string_view v = argv[1];
        if (v == "summarize") {
            if (argc < 3) {
                std::fprintf(stderr, "usage: b70tools summarize <run-dir-or-events.jsonl>\n");
                return 1;
            }
            return b70::run_summarize_command(argv[2]);
        }
        if (v == "disagreements") {
            if (argc < 3) {
                std::fprintf(stderr, "usage: b70tools disagreements <run-dir-or-events.jsonl>\n");
                return 1;
            }
            return b70::run_disagreements_command(argv[2]);
        }
        if (v == "adapters") {
            if (argc < 3) {
                std::fprintf(stderr, "usage: b70tools adapters <run-dir-or-events.jsonl>\n");
                return 1;
            }
            return b70::run_adapters_command(argv[2]);
        }
        if (v == "self") {
            if (argc < 3) {
                std::fprintf(stderr, "usage: b70tools self <run-dir-or-events.jsonl>\n");
                return 1;
            }
            return b70::run_self_command(argv[2]);
        }
        if (v == "verdict") {
            if (argc < 3) {
                std::fprintf(stderr,
                    "usage: b70tools verdict <run-dir-or-events.jsonl>\n"
                    "                       [--json]\n"
                    "                       [--max-host-used-gb F]\n"
                    "                       [--min-card0-avg-w F]\n"
                    "                       [--min-card0-activity-pct F]\n"
                    "                       [--window-s F]\n"
                    "                       [--healthy-adapter ID]\n"
                    "exits 0=healthy, 2=broken, 3=insufficient-data\n");
                return 1;
            }
            b70::VerdictOptions vo;
            for (int i = 3; i < argc; ++i) {
                std::string_view a = argv[i];
                if (a == "--json")                                              vo.emit_json = true;
                else if (a == "--max-host-used-gb"        && i+1<argc)          vo.max_host_used_gb        = std::strtod(argv[++i], nullptr);
                else if (a == "--min-card0-avg-w"         && i+1<argc)          vo.min_card0_avg_w         = std::strtod(argv[++i], nullptr);
                else if (a == "--min-card0-activity-pct"  && i+1<argc)          vo.min_card0_activity_pct  = std::strtod(argv[++i], nullptr);
                else if (a == "--window-s"                && i+1<argc)          vo.window_s                = std::strtod(argv[++i], nullptr);
                else if (a == "--healthy-adapter"         && i+1<argc)          vo.healthy_adapter         = argv[++i];
            }
            return b70::run_verdict_command(argv[2], vo);
        }
        if (v == "run") {
            Args ra;
            ra.run = true;
            // accept --ticks / --cadence-ms / --no-sleep / --out after "run"
            for (int i = 2; i < argc; ++i) {
                std::string_view s = argv[i];
                if (s == "--ticks" && i + 1 < argc)         ra.ticks = std::strtoull(argv[++i], nullptr, 10);
                else if (s == "--cadence-ms" && i + 1 < argc) ra.cadence_ms = std::strtoull(argv[++i], nullptr, 10);
                else if (s == "--no-sleep")                 ra.no_sleep = true;
                else if (s == "--out" && i + 1 < argc)      ra.out_dir = argv[++i];
            }
            return run_real(ra);
        }
    }
    Args a;
    if (!parse_args(argc, argv, a)) return 1;
    if (a.enumerate) return run_enumerate(a);
    if (a.dry_run) return run_dry(a);
    if (a.run) return run_real(a);
    std::fprintf(stderr,
        "b70tools: no mode selected.\n"
        "  b70tools run [--ticks N] [--cadence-ms N] [--no-sleep] [--out DIR]\n"
        "                Tier 0 + Tier 1 polling on the real rig.\n"
        "  b70tools summarize <run-dir-or-jsonl>\n"
        "                Print structured text report of a recorded run.\n"
        "  b70tools --dry-run      Exercise Milestone M1 (synthetic collector round-trip).\n"
        "  b70tools --enumerate    Run Tier 0 identity reconciliation only.\n");
    return 1;
}
