# b70tools — Plan v2

**Status:** plan only. No code yet. Open to redirection.
**Date:** 2026-05-28 (v0 → v1 → v2 same day, after user feedback rounds).
**Document scope split:** This document has three parts. **Only Part A (v1) is in scope for first implementation.** Parts B and C are intent, not commitments.

- **Part A — v1: Passive Observer** (the only thing that ships first).
- **Part B — v1.5: Optional Extensions** (added incrementally, only if v1 proves useful).
- **Part C — v2 / Future Architecture** (recorded so we don't lose ideas; not built until justified by operational pain).

---

## Target host (concrete, confirmed 2026-05-28)

- **GPUs:** 2× **Intel Arc Pro B70** (the actual card model — the project name `b70tools` derives directly from this). Battlemage / Xe2 workstation tier — 32 Xe Cores, 32 Ray Tracing Units, 256-bit GDDR6, **PCIe 5.0 ×16 card capability** (negotiated down on this host — see board note). **32 GB VRAM per card raw**; with ECC enabled, **28 GB usable per card** (ECC overhead = 4 GB/card). Total VRAM pool: 56 GB (ECC on) / 64 GB (ECC off). ECC mode is a per-card runtime setting and **must be probed, not assumed**.
- **CPU:** AMD Ryzen 9 5900X, 12C/24T, 3.7 GHz.
- **Board / chipset:** Gigabyte X570 AORUS ULTRA (AM4, **PCIe 4.0** host). Cards are PCIe 5.0-capable but **negotiate down to PCIe 4.0** on this platform; with 2 GPUs the typical CPU-side config is x8/x8. The gap between "card max capability" and "negotiated link state" is itself a fingerprint signal worth capturing — see §A.11.
- **System RAM:** 32 GB DDR4 (Available ≈ 20.7 GB at observation time). Matches the 20–24 GB inference-budget assumption.
- **OS:** Windows 10 Pro 10.0.19045.
- **Page file:** 16 GB on C:\.
- **Security posture:** Secure Boot On; Kernel DMA Protection On; **WDAC policy Enforced**; Hyper-V virtualization-in-firmware Off.

**Memory math:** Per-card `DedicatedVideoMemory ≈ 32 GB` → Task-Manager-style sum `32 + SharedSystemMemory(≈16) = 48 GB` reproduces the symptom exactly. "Effective safe inference capacity" subtracts 4 GB/card whenever ECC is detected ON.

**Language:** C++. Direct access to Vulkan / IGCL / DXGI / WDDM / SetupAPI / D3DKMT.

---

# Part A — v1: Passive Observer

## A.1 Mission (v1)

**Lightweight and functional is the overriding requirement.** v1 is a passive observer of multi-GPU Vulkan inference on this rig. Its job is to provide useful insight without becoming a confounding variable in the experiments it is measuring.

If b70tools over-polls, over-queries, wakes adapters, creates extra contexts, spawns hidden workers, increases residency pressure, perturbs clocks, changes power behavior, or meaningfully affects tokens/sec, it has defeated its purpose.

v1 optimizes for **stable low-overhead observability during real inference**, not for completeness or research depth.

## A.2 "Do no harm" budget

| Target | Default | Stretch |
|---|---|---|
| Resident memory (RSS) | < 50 MB | < 30 MB |
| CPU | < 0.5% of one core at 1 Hz | — |
| Throughput perturbation vs. baseline | < 1% (measured later; assumed in v1) | — |
| GPU memory allocations | **zero** | — |
| GPU contexts created (`VkDevice`, `VkQueue`, command pools) | **zero** | — |
| Additional long-lived subprocesses | **zero** | — |
| Always-on heavy tracing (ETW, PresentMon, Vulkan perf queries) | **off** | — |
| Per-source background worker threads | **forbidden** (one collection thread, one writer, one watchdog — total) | — |

Any v1 collector that cannot meet these has to be moved to v1.5 or v2.

## A.3 Six questions v1 must answer

These are the user-facing outputs. Everything else is supporting infrastructure.

1. **Which physical adapters exist** on this host right now?
2. **Can we correlate them reliably** across Vulkan, DXGI, IGCL, SetupAPI, PDH?
3. **Are both adapters awake during inference?** (Distinct from "Windows says they're active.")
4. **Is memory pressure rising** on one or both adapters?
5. **Do sources disagree in obvious ways?** (48-GB pattern, NaN, active-but-idle, identity drift.)
6. **Did a TDR, reset, identity drift, or impossible memory report occur** during the observed run?

## A.4 Glossary (v1)

| Term | Definition |
|---|---|
| **Dedicated VRAM** | Physical memory soldered to the GPU board. `DXGI_ADAPTER_DESC3.DedicatedVideoMemory`. Stable. |
| **Shared system memory** | Host RAM WDDM is willing to expose as non-local. `DXGI_ADAPTER_DESC3.SharedSystemMemory`. Not physically on the GPU. |
| **WDDM `LOCAL` / `NON_LOCAL`** | WDDM's device-local vs. system-RAM-backed segments. Surfaced via `IDXGIAdapter3::QueryVideoMemoryInfo`. |
| **Vulkan `DEVICE_LOCAL` heap** | Vulkan abstraction of fast/local memory; on discrete cards ≈ Dedicated VRAM, possibly slightly smaller due to driver reservations. |
| **ECC mode** | Per-card runtime setting on Arc Pro B70. When ON, 4 GB/card is consumed by ECC syndrome storage. **Probed per session, not assumed.** |
| **ECC overhead** | Bytes lost to ECC when ECC=ON. Subtracted from per-card capacity in headroom calculations. |
| **PCIe link state** | Negotiated PCIe gen + width per adapter. Identity-domain metric. |
| **Adapter awake** | An arbitration-level boolean — distinct from "Windows says active." See A.6 (AdapterState FSM). |
| **Effective allocatable memory** | Per-source, per-process, per-heap budget the source currently claims is available *to this process*. |
| **Effective safe inference capacity** | b70tools-derived headroom estimate per card. Subtracts ECC overhead + other-process residency estimate + safety margin. Not produced by any single source. |

## A.5 Identity reconciliation

**Primary reconciliation anchor: Windows LUID (8 bytes).** Chosen for cross-API utility, not because it is "truth." No layer on this system is absolute truth.

| API | How to obtain LUID |
|---|---|
| DXGI | `IDXGIAdapter1::GetDesc1` → `AdapterLuid` |
| Vulkan | `VkPhysicalDeviceIDProperties.deviceLUID` (spec §4 mandates byte-equality with DXGI on Windows; assert + warn if invalid) |
| IGCL | `ctl_device_adapter_properties_t.pDeviceID` |
| WDDM/PDH | Counter instance name embeds `luid_0x<HighPart>_0x<LowPart>` |
| SetupAPI | `CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_Gpu_Luid, …)` |

**Secondary anchor: PCI BDF** via SetupAPI `DEVPKEY_Device_LocationInfo` from the LUID-matched devnode. Used for cross-checking and (later) for L0/Sysman binding.

Reconciliation runs once at startup. Result: one `AdapterIdentity` record per physical GPU listing the bindings + the evidence chain that produced each one. **Ambiguous binding refuses startup** and emits a structured "ambiguous identity" report with a remediation hint.

**Enumeration order is never trusted** across DXGI / Vulkan / PDH.

## A.6 AdapterState — first-class FSM

Promoted from glossary to a first-class arbitration state per adapter, updated every tick.

```
AdapterState ∈ {
  Unknown,                     // pre-first-evaluation
  Idle,                        // awake but no work observed
  Awake,                       // clocks/power/temp non-trivial, but no engine activity yet
  ActiveCompute,               // engine activity counter rising
  SuspectedComputeHidden,      // clocks + power up, but every util source reports zero
                               //   (the "0% util while computing" symptom)
  PostTDR,                     // VK_ERROR_DEVICE_LOST or D3DKMT power-state-override transition seen
  Lost,                        // adapter missing from enumeration in current tick
  Reenumerating                // recovery in progress
}
```

Transition triggers (v1):

- `Unknown → Idle` after first successful Tier 1 evidence pass.
- `Idle ↔ Awake` driven primarily by **D3DKMT `ADAPTERPERFDATA`** (Power, MemoryFrequency, Temperature). The FSM does **not** require IGCL to advance through these states — D3DKMT alone is sufficient.
- `Awake → ActiveCompute` driven by whichever signal is healthy this tick, in this preference order: PDH compute-engtype delta (if `pdh_gpu_engine_lite` enabled) → IGCL `renderComputeActivityCounter` delta (if IGCL healthy) → derived inference from `MemoryBandwidth` + `Power` rise (if neither util source is available, this is a `Low`-confidence transition emitted with `observation_kind = Inferred`).
- `Awake/ActiveCompute → SuspectedComputeHidden` when the active-but-idle disagreement rule fires (clocks ≥ 80% max **and** Power > 30% **and** every util source reports zero over a 5 s window).
- `Any → PostTDR` on `VK_ERROR_DEVICE_LOST` or D3DKMT `PowerStateOverride` transition.
- `Any → Lost` if the adapter disappears from a re-enumeration tick.
- `Lost → Reenumerating → Idle/Awake` on recovery; **bumps session epoch.**

State transitions emit `AdapterStateTransition` JSONL records (never delta-suppressed).

## A.7 MetricSample schema (v1)

```
MetricSample {
  metric_name       : stable string ID
  adapter_id        : stable internal ID from AdapterIdentity
  session_epoch     : u32
  semantic_domain   : enum

  value             : union { u64, f64, Missing-sentinel }
  unit              : enum (bytes, hz, watts, celsius, pct, dimensionless, …)

  source            : enum
  source_detail     : optional string
  timestamp_qpc     : u64                  // instant the poll() call returned
  poll_latency_ns   : u64                  // wall time the source call took
  sampling_window_ns: u64                  // semantic window the VALUE represents
                                           //   = 0   for instantaneous snapshots
                                           //   = Δt  for counter-delta-derived values (e.g. activity %)
                                           //   = N   for rolling averages reported by the source over N ns

  observation_kind  : enum (DirectlyObserved, DerivedFromDelta, Inferred, Estimated, Reported_Untrusted)
  correlation_method: enum (LUID_DirectBind, BDF_Match, ProcessID_Filter, DriverHandle, Unbound)
  confidence        : enum (High, Medium, Low, Disagreed)
  flags             : bitset (Stale, PostTDR, FirstSampleAfterEnum, BudgetExceeded,
                              SourceEmittedNaN, SourceEmittedInf, PDH_CStatus_NonZero, …)
}
```

`semantic_domain` (v1 set): `Identity, Memory, Power, Thermal, Frequency, EngineActivity, Process, Driver, Arbitration`. The richer set (QueueActivity, Residency, PCIe, Allocation, Fault, Inference) is reserved for v1.5/v2.

NaN/Inf are never propagated as float bit patterns — they're coerced to `Missing-sentinel` with the appropriate flag.

## A.8 Collector side-effects taxonomy

Every collector — even passive ones — declares side-effects metadata, **and the library audit verifies it**.

```
CollectorSideEffects {
  creates_vk_device          : bool
  creates_vk_queue           : bool
  creates_command_pool       : bool
  allocates_gpu_memory       : bool
  spawns_threads_on_init     : bool   // declared; audited
  may_wake_idle_adapter      : bool
  may_alter_power_state      : bool
  may_alter_clocks           : bool
  may_increase_residency     : bool
  triggers_driver_refresh    : bool
  intrusiveness              : enum (PassiveSafe | PassiveButMayWake | Probe | Intrusive)
}
```

**Intrusiveness levels (sharpened from "Passive/Probe" — these are now operationally distinct):**

| Level | Definition |
|---|---|
| `PassiveSafe` | Read-only. No GPU-context creation. No adapter wake. No persistent driver interaction. No telemetry loop inside vendor runtime. Safe to poll continuously without perturbing the workload. |
| `PassiveButMayWake` | Read-only from our side, but the call may wake an idle adapter, trigger a driver refresh path, or instantiate hidden telemetry state inside the vendor runtime. May alter clocks/power state slightly. **IGCL falls here, not in `PassiveSafe`** — even if it is lightweight, it is not truly inert. |
| `Probe` | Performs an active read against the device that may briefly contend with workload submissions (e.g. timestamp queries on a shared queue). Not used in v1. |
| `Intrusive` | Creates GPU contexts, allocates GPU memory, or otherwise structurally changes the system being observed. Not used in v1. |

**v1 default: every enabled collector must be `PassiveSafe` or `PassiveButMayWake`.** `Probe` and `Intrusive` collectors require explicit opt-in (and are v1.5/v2 anyway — none planned for v1). For `PassiveButMayWake` collectors (i.e. IGCL), the user can disable them globally and v1 must still answer all six questions of A.3 — see fallback hierarchy in §A.10.

If the library audit (A.12) observes side-effects the collector didn't declare, the collector is disabled for the session and a structured warning is emitted.

### Polling fan-out — explicitly forbidden inside collectors

A v1 collector may **NOT**:
- Spawn internal polling worker threads.
- Batch-refresh hidden telemetry state asynchronously.
- Maintain rolling caches updated outside the synchronous `poll()` call.
- Schedule background timers.
- Multiplex multiple device handles via internal parallelism.

The invariant is **deterministic low-overhead synchronous observability**. Every observation is initiated by the single collection thread's `poll()` call and completes before the next collector runs. Collectors discovered violating this in the library audit are disabled for the session.

## A.9 Replay-first event flow

The internal event bus is canonical. Live collectors and replay readers both feed the same bus. Arbitrator and writer are source-agnostic.

```
[Live collectors] ──┐
                    ├──► canonical event bus ──► [Arbitrator] ──► [JSONL writer]
[Replay reader] ────┘                                           ──► [stdout summary, optional]
```

Events on the bus:

- `MetricSample`
- `AdapterIdentity` (once + on drift)
- `AdapterStateTransition`
- `DisagreementReport`
- `SessionEpochBoundary`
- `DriverRuntimeFingerprint` (once at startup)
- `CollectorAuditRecord`

This separation is cheap to do up front (one interface, no extra runtime cost) and makes the arbitrator testable without hardware. Test fixtures themselves are v1.5 — but the **plumbing for replay is v1**.

## A.10 v1 collectors (the entire list)

Every collector below has been chosen because (a) it answers one of the six questions in A.3, (b) its declared side-effects are `Passive` or `Probe`, and (c) it does not require creating a `VkDevice`.

### Tier 0 — Static enumeration (once at startup)

| Collector | Purpose | Side-effects |
|---|---|---|
| `dxgi_enum` | Adapters, LUIDs, descriptions, `DedicatedVideoMemory`, `SharedSystemMemory`, `DedicatedSystemMemory` | Passive |
| `setupapi_devnode_walk` | LUID ↔ PCI BDF binding, devnode paths | Passive |
| `vulkan_enum` | Physical device enumeration, ID props, memory-properties **layout**, queue families, extension list, timestamp valid bits. **No `VkDevice` created.** | Passive |
| `extension_probe` | Presence/absence: `VK_EXT_memory_budget`, `VK_EXT_pci_bus_info` (likely absent), others reserved for future | Passive |
| `igcl_init` | Bind IGCL handles → LUID via `pDeviceID`; **library audit gate**: if init spawns threads we can't trace, disable for session | Probe |
| `driver_fingerprint` | One-shot capture: see A.11 | Passive |
| `identity_reconciler` | Build cross-API binding table; refuse on ambiguity | Passive (in-process logic only) |

### Tier 1 — Default-on polling (1000 ms cadence ±25–50 ms jitter)

| Collector | What it returns | Side-effects | Notes |
|---|---|---|---|
| `vulkan_memory_budget` | Per-heap `heapUsage` + `heapBudget` for this process, via `vkGetPhysicalDeviceMemoryProperties2` with `VkPhysicalDeviceMemoryBudgetPropertiesEXT` chained. **Uses `VkPhysicalDevice` only — no `VkDevice` needed.** | Passive | Per-process only — see Vulkan visibility caveat below |
| `dxgi_query_video_memory` | `LOCAL` + `NON_LOCAL` budget/usage per adapter, this process | Passive | Per-process |
| `d3dkmt_adapter_perfdata` | `MemoryFrequency`, `MaxMemoryFrequency`, `MemoryBandwidth`, `PCIEBandwidth`, `FanRPM`, `Power` (‰), `Temperature` (deci-°C), `PowerStateOverride` | Passive | **The "Adapter awake" sentinel.** Documented Win32. |
| `igcl_power_telemetry` | Per device in one IOCTL: energy, voltage, gpu/vram clocks, gpu/vram temps, render-compute/media activity counters, VRAM r/w bandwidth | **PassiveButMayWake** | Gated on library audit. **Optional preferred Intel path — not architectural backbone.** v1 must remain useful with this collector disabled. |

### Conditional Tier 1 (default OFF in v1, promotable based on measured overhead)

| Collector | Why conditional |
|---|---|
| `pdh_gpu_engine_lite` | System-wide engine util (sum, not "busiest"). **Adds PDH refresh cost — measure first.** If it lands inside the do-no-harm budget on this rig, promote to default-on in v1.1; otherwise keep diagnostic-only. |

### Signal-source fallback hierarchy

Intel workstation telemetry stacks are historically inconsistent across driver revisions, Arc generations, firmware, Windows builds, security policy states, and background Intel software presence. **v1 must remain useful even if IGCL is disabled entirely.** Every question of A.3 has a non-IGCL path.

Fallback preference order per signal class (most-trusted-and-cheapest first):

1. **D3DKMT `ADAPTERPERFDATA`** — `PassiveSafe`, documented Win32, the "is the card awake" sentinel. Carries Power, MemoryFrequency, Temperature, MemoryBandwidth, PCIEBandwidth. **Backbone.**
2. **DXGI `QueryVideoMemoryInfo`** — `PassiveSafe`, own-process per-segment budget/usage. Backbone for Memory domain.
3. **Vulkan `VK_EXT_memory_budget`** — `PassiveSafe` (no `VkDevice`), own-process per-heap. Cross-check for DXGI; adds heap-layout context DXGI lacks.
4. **IGCL `ctlPowerTelemetryGet`** — `PassiveButMayWake`. Bonus telemetry (per-engine activity, VRAM r/w bandwidth, voltages). **Disabled gracefully if library audit fails, if WDAC blocks the DLL, or if user opts out.**
5. **PDH `GPU Engine` lite** — `PassiveSafe`, conditional-T1. Used for system-wide engine util when IGCL is unavailable or as cross-check when it is. Promoted only if measured overhead fits the budget.

**The AdapterState FSM, all six disagreement rules, and answers to all six A.3 questions function with only sources 1–3 present.** IGCL and PDH improve confidence and refine the `Awake → ActiveCompute` transition, but their absence does not break v1.

### Vulkan visibility caveat (foundational)

`VK_EXT_memory_budget` sees **only what is allocated through Vulkan in this process**. It does not represent:

- Total adapter residency.
- Allocations from other APIs (D3D, OpenCL, OneAPI, ML runtimes).
- Hidden driver / compositor / DWM allocations.
- Full WDDM pressure.

Strong own-process signal. Weak system-wide signal. Documented in every Memory-domain `MetricSample` that originates from Vulkan via `source = Vulkan_MemoryBudget` + `correlation_method = ProcessID_Filter`.

### Vulkan policy (v1)

- **Tier 0 and Tier 1 must not create `VkDevice`.** Enumeration + properties-chain queries on `VkPhysicalDevice` only.
- Tier 2+ may create isolated research-mode `VkDevice`s but must declare: device ownership, queue usage, residency implications, synchronization behavior. **None planned in v1.**

## A.11 Driver/runtime fingerprint (one-shot at startup)

Captured once, emitted as `DriverRuntimeFingerprint` JSONL record, included in the session envelope. Reproducibility hinges on this.

| Field | Source |
|---|---|
| Windows build | already known (10.0.19045) |
| BIOS version + date | SMBIOS via WMI (`Win32_BIOS`) |
| Above 4G Decoding | BIOS-side; inferred from large BAR sizes in PCI config / from ReBAR effectiveness |
| ReBAR enabled per adapter | DXGI BAR size vs. DedicatedVideoMemory |
| **Largest `DEVICE_LOCAL \| HOST_VISIBLE` heap size per device** | Vulkan memory properties walk. Direct measurement of how much VRAM the CPU can map. |
| **CPU-visible VRAM percentage per device** | `(largest DEVICE_LOCAL \| HOST_VISIBLE heap) / DedicatedVideoMemory`. Single derived number — the operational thing that actually matters for large model loads and host-side residency strategies. |
| **Full-ReBAR active boolean per device** | True iff CPU-visible VRAM % ≥ 95%. False if capped near the 256 MB historical aperture (the known Intel Arc ReBAR-in-Vulkan symptom). Treated as one of the most load-bearing fingerprint signals on this rig. |
| PCIe gen + width: **max card capability** | Static; card spec (PCIe 5.0 ×16 for B70). Record from IGCL PCI props if exposed, else hard-coded from card-model lookup |
| PCIe gen + width: **currently negotiated** | IGCL PCI props (preferred); fall back to L0/Sysman if v1.5; SetupAPI as last resort. **Expected on this host: PCIe 4.0 ×8 per card.** Discrepancy from max → fingerprint signal |
| Intel driver package version | Registry `HKLM\SOFTWARE\Intel\GFX` + DXGI `DriverVersion` |
| Vulkan runtime version | `vkEnumerateInstanceVersion` |
| Vulkan ICD path + version | Loader manifest enumeration |
| IGCL DLL path + version | Resolved DLL file properties |
| HAGS (Hardware-Accelerated GPU Scheduling) | Registry `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\HwSchMode` |
| MPO (Multi-Plane Overlay) | Registry under `GraphicsDrivers\Scheduler` (if detectable) |
| WDAC policy state | `WldpQueryWindowsLockdownMode` / `CiQueryInformation` |
| ECC mode per adapter | IGCL ECC property (if exposed); else infer from DedicatedVideoMemory rounding (28 vs. 32 GB) |
| TDR registry settings | `HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Tdr*` |
| Page file config | `Win32_PageFileSetting` |

Many disagreement patterns correlate with fingerprint changes (BIOS, driver, ReBAR, HAGS) — capturing this enables post-hoc reproduction across sessions.

## A.12 Library audit

Library audit is in v1 (downscoped from v1.5 — it's load-bearing for the "do no harm" promise).

For each Tier 0 / Tier 1 collector init:

1. Snapshot process thread count + loaded module list **before** init.
2. Run init.
3. Snapshot **after**.
4. Emit `CollectorAuditRecord` (Arbitration domain) listing: threads added (start address, owning module), modules transitively loaded, init wall time, RSS delta.
5. Cross-check observed side-effects against the collector's declared `CollectorSideEffects`. **Disagreement disables the collector for the session.**
6. If audit observes a thread we can't attribute, disable the collector unless `--allow-unknown-threads` was passed.

## A.13 Output format

JSONL streamed to disk. Delta-suppression on by default. **30 s heartbeat snapshot** for recoverability.

- Compact single-byte field keys on disk; `schema.json` written once per session maps short → canonical names.
- Always-emitted records (never delta-suppressed): `AdapterIdentity`, `AdapterStateTransition`, `DisagreementReport`, `SessionEpochBoundary`, `DriverRuntimeFingerprint`, `CollectorAuditRecord`.
- Rotation: 100 MB chunks; gzip on rotation; keep last 14.
- Default output path: configurable; recommend `D:\` not `C:\` to spare the system SSD.

Expected steady-state disk: **3–15 KB/s ≈ 250 MB – 1.3 GB/day**.

## A.14 Disagreement rules (v1 — small set, deliberately)

| Rule | Trigger | Action |
|---|---|---|
| **48-GB pattern** | Any source reports VRAM total > `DedicatedVideoMemory × 1.05` | Coerce to `Reported_Untrusted`; record "summing DedicatedVideoMemory + SharedSystemMemory" |
| **NaN/Inf injection** | Any IEEE float source produces NaN/Inf | Coerce to Missing-sentinel + appropriate flag |
| **Active-but-idle** | Clocks ≥ 80% max **AND** Power > 30% **AND** all util sources report 0 over 5 s | Promote D3DKMT-derived state to ground-truth for "awake"; transition AdapterState → `SuspectedComputeHidden` |
| **Identity drift** | LUID present at T, absent at T+1, no hot-unplug event | Bump session epoch; re-run Tier 0; refuse to assert identity until reconciled |
| **Post-TDR** | `VK_ERROR_DEVICE_LOST` OR D3DKMT `PowerStateOverride` transition | AdapterState → `PostTDR`; bump session epoch; drop suspect samples |
| **Impossible heap budget** | `heapBudget > DedicatedVideoMemory × 1.5` | Confidence: Disagreed; flag |

That's the v1 set. Six rules. Everything else is v1.5+.

## A.15 v1 resource budget

For this rig (2× Arc Pro B70, 1 Hz cadence, Tier 0+1 only):

| Resource | Estimate |
|---|---|
| CPU | ~3–5 ms wall/tick (~0.3–0.5% of one core) |
| RSS steady state | ~20–30 MB (Vulkan + IGCL + DXGI loaded + write buffer) |
| Disk write rate | 3–15 KB/s; 250 MB – 1.3 GB/day |
| Process count | 1 |
| Threads | 3 total (collection + writer + watchdog) |
| GPU memory allocations | **0** |
| GPU contexts created | **0** |

Margin against the do-no-harm budget: comfortable. The behavioral constraints (no `VkDevice`, no allocations, no extra workers) are the binding ones, not RAM.

## A.16 Process model (v1)

**Single statically-linked executable. Internal modular collector interfaces. Lazy initialization.**

- One process, one binary (`b70tools.exe`).
- Collectors are internal modules behind a common C++ interface — not external DLLs.
- Lazy init: a collector's `init()` runs only when the collector is enabled by config.
- A collector that fails `init()` or library audit is disabled for the session; the process continues with the rest.
- Watchdog thread enforces per-collector deadlines with **minimal logic**: detect stall → mark collector degraded → skip future polling of that collector for this session → emit a single `Arbitration`-domain event → continue with the rest. **No forced thread interruption. No aggressive cancellation. No collector resurrection. No partial-recovery orchestration.** v1 is an observer, not a fault-tolerant kernel.
- **No SEH sandboxing of collector calls in v1** — fail-fast + crash dump + session restart is simpler and safer than building a microkernel inside a telemetry tool.
- No vendor-DLL crash recovery beyond watchdog-driven drop and session restart by user. If a vendor DLL is unstable enough to crash us repeatedly, that fact itself is the finding.

DLL collectors and SEH isolation are deferred to v2 (C.x). They become justified only if a vendor library proves chronically unstable in operational use.

## A.17 Project structure (v1)

```
b70tools/
  docs/
    plan.md
    glossary.md            (extracted from A.4 for quick reference)
  src/
    main.cc                (CLI, signal handling, lifecycle)
    schema/
      metric_sample.h
      adapter_id.h
      adapter_state.h      (the FSM)
      events.h             (all canonical event types)
      jsonl_writer.h
      delta_filter.h
      compact_format.h
    bus/
      event_bus.h          (single canonical bus; lets replay/live share arbitrator)
    identity/
      dxgi_enum.cc
      setupapi_devnodes.cc
      vulkan_enum.cc
      igcl_enum.cc
      reconciler.cc
    collectors/            (internal modules — NOT DLLs)
      vulkan_memory_budget.cc
      dxgi_query_video_memory.cc
      d3dkmt_adapter_perfdata.cc
      igcl_power_telemetry.cc
      library_audit.cc
      driver_fingerprint.cc
    arbitrator/
      adapter_state_fsm.cc
      disagreement_rules.cc
      confidence.cc        (per-tick rules only in v1)
    runtime/
      poll_loop.cc
      watchdog.cc
      session.cc
      replay_reader.cc     (interface present; fixture suite is v1.5)
  third_party/
    Vulkan-Headers/
    igcl/                  (intel/drivers.gpu.control-library)
  CMakeLists.txt
```

Build: CMake + MSVC (or clang-cl). No third-party JSON library. Static linking; no plugin DLLs.

## A.18 v1 deliverable order

Implementation order — earliest dependency first. Two milestones (M1, M2) are called out explicitly.

1. `schema/` — event types, MetricSample (with `sampling_window_ns`), AdapterState, JSONL writer (with delta + heartbeat).
2. `bus/` — canonical event bus interface (so arbitrator is replay-ready from day 1).
3. **`collectors/fake_collector.cc`** — synthetic source emitting deterministic metrics, synthetic disagreement states, synthetic `AdapterStateTransition`s, timing jitter, NaN injection. **No GPU APIs touched.** Validates schema, bus, JSONL, delta suppression, arbitrator skeleton, watchdog, replay reader before any vendor API is involved.
4. `arbitrator/adapter_state_fsm.cc` (skeleton) + `arbitrator/disagreement_rules.cc` (the six rules) — driven by `fake_collector` first, then by real collectors as they land.
5. `runtime/poll_loop.cc` + `runtime/watchdog.cc` (minimal; see §A.16) + `runtime/session.cc`.

### **🟢 Milestone M1 — "first synthetic telemetry packet"**
Definition (all of the following true in a single dry run):
- Process starts.
- Schema initialized, JSONL writer opened.
- `fake_collector` emits one `MetricSample`.
- One JSONL line written to disk.
- One `AdapterStateTransition` emitted.
- Replay reader can read it back and feed the bus.
- RSS within do-no-harm budget.
- No `VkDevice`, no GPU allocation, no GPU APIs called at all.

This is the **first real success state for the project** — proves the entire non-vendor architecture before any Intel/Windows API risk.

---

6. `identity/` — DXGI enum, SetupAPI devnodes, Vulkan enum (no `VkDevice`), reconciler. Answers A.3 questions 1 + 2.
7. `collectors/d3dkmt_adapter_perfdata.cc` — **`PassiveSafe`** "Adapter awake" sentinel; answers question 3 without IGCL.
8. `collectors/dxgi_query_video_memory.cc` + `collectors/vulkan_memory_budget.cc` — memory pressure (PassiveSafe); answers question 4.

### **🟢 Milestone M2 — "first trustworthy telemetry packet"**
Definition (all of the following true on the real rig):
- DXGI adapter identity captured for both Arc Pro B70s.
- Vulkan physical-device identity captured.
- Stable LUID reconciliation across DXGI / Vulkan / SetupAPI.
- One `MetricSample` from D3DKMT `ADAPTERPERFDATA` emitted successfully.
- JSONL line written.
- `AdapterState` advances from `Unknown → Idle` (or → `Awake` if a card is busy).
- No `VkDevice`. No GPU allocation. RSS within budget.

This is the project's **first real-world success state.** Everything beyond this is enhancement.

---

9. `collectors/library_audit.cc` + `collectors/driver_fingerprint.cc` (incl. ReBAR / host-visible-heap-size / CPU-visible-VRAM-% capture).
10. `collectors/igcl_power_telemetry.cc` — `PassiveButMayWake`; gated on library-audit cleanliness; v1 must still answer all six A.3 questions if this collector is disabled.
11. `main.cc` + CLI.

Each step is shippable. **M1 ships before any vendor API is touched.** M2 is the canonical "v1 is working" point.

## A.19 Out of scope (v1, hard line)

- ETW.
- PresentMon child process.
- Vulkan performance queries (`VK_KHR_performance_query`, `VK_INTEL_performance_query`).
- Calibrated timestamps (`VK_KHR_calibrated_timestamps`).
- Any creation of `VkDevice` / `VkQueue` / command pools / memory allocations.
- DLL plugin collectors.
- SEH-sandboxed collector calls.
- Per-source background workers.
- Source reputation evolution.
- InferenceSemanticEvent layer beyond the schema reservation (none emitted in v1).
- GUI.
- Cross-machine telemetry.
- Linux.
- Non-Intel GPU collectors.
- Bench mode (`--bench`).
- Replay regression-test fixtures (the **interface** is in v1; the **fixtures + assertions** are v1.5).
- Control-plane operations (setting clocks / power limits / fan curves).

---

# Part B — v1.5: Optional Extensions

Added incrementally only after v1 ships and proves useful. Each item is gated on a real operational need observed during v1 use.

- **Replay regression-test fixture suite.** Named scenarios with input fixtures + expected-output assertions: `tm-48gb-on-32gb-card`, `active-clocks-zero-util`, `nan-telemetry`, `adapter-reorder`, `post-tdr-stale`, `gpu-invisible-to-windows`, `heap-budget-mismatch`, `sleep-wake-recovery`, `vk-rebar-cap-at-256mb`, `collector-hang`. The replay *reader* is v1; the fixture suite + CI assertions are v1.5.
- **Bench mode (`--bench`)** — observer perturbation measurement. Baseline / Telemetry-on / Stress windows back-to-back; report throughput delta. Requires workload-side throughput counter.
- **L0 / Sysman** as second-opinion cross-check against IGCL (per-engine-subgroup activity, independent thermal/power reading).
- **PDH `GPU Process Memory`** full set — per-PID per-LUID committed/local/non-local, the "who else is using this card?" view.
- **D3DKMT `QueryStatistics`** — per-segment commit, allocation counts, page-in/page-out rates.
- **Promotion of `pdh_gpu_engine_lite`** from conditional-T1 to default-T1 once measured overhead is confirmed within budget.
- **Additional disagreement rules**: over-budget budget, ReBAR-cap-at-256MB heap symptom, more nuanced post-TDR detection.
- **InferenceSemanticEvent `ContextPressureRise` emission** — the simplest semantic-event producer; only this one in v1.5.
- **Diagnostic mode (`--diagnostic`)** — enables Tier 2 collectors above for short investigations.
- **ECC overhead subtraction in `Effective safe inference capacity`** computation (schema reserves it in v1; arithmetic lands in v1.5).
- **WDAC-aware DLL loading** — only if v2 DLL collectors are ever introduced.

---

# Part C — v2 / Future Architecture

Recorded so we don't lose the ideas. **Not built unless operational pain justifies the cost.** Each carries real complexity and is easily over-engineered.

## C.1 Dynamic collector DLL model

External DLL plugins behind a stable C-ABI. Per-collector `bt_collector_*.dll`. Motivation: load-on-demand isolation, vendor-library updatability without rebuild. **Cost:** ABI churn, build complexity, loader-order bugs, WDAC signing requirements on this host, debugging friction. Defer until at least one vendor library proves chronically unstable in v1 operational use.

## C.2 SEH-sandboxed collector calls

Structured exception handling around each call into a fragile vendor library. **Risk:** unsafe across C++ boundaries, fragile with compiler/runtime differences, stack-corruption hazards. Building a microkernel inside a telemetry tool. Defer unless v1's fail-fast + watchdog model demonstrably fails.

## C.3 Source reputation evolution

Per-source, per-domain historical trust scores (EWMA, bounded [0, 1]). Arbitrator weights disagreements by reputation rather than treating sources symmetrically. v1 uses static per-tick rules; v2 adds learned trust.

## C.4 InferenceSemanticEvent layer (full)

Beyond `ContextPressureRise`: `ModelLoaded`, `KVCacheExpanded`, `ThroughputCollapse`, `LayerSplitChanged`, `GPUImbalanceDetected`, `SuspectedCPUFallback`, `ResidencyPressureIncrease`, `RecoveryFromTDR`. Each is a rule producing an event from accumulated MetricSamples + (optionally) workload-side signals.

## C.5 Research / Profiler mode

ETW real-time consumer (narrow-keyword `Microsoft-Windows-DxgKrnl` capture window). PresentMon CLI as child. Vulkan calibrated timestamps + timestamp queries (requires isolated `VkDevice`). `VK_EXT_device_fault` post-TDR captures. **Intrusive collectors — explicit opt-in only; never default-on.**

## C.6 GPA / vendor-profiler integration

Intel GPA, Vulkan vendor performance queries when exposed. Same caveats as C.5 — these change the system being observed.

## C.7 Production-runtime hardening

Multi-host telemetry, structured remote shipping, dashboards. Only if b70tools graduates from "this rig" to multi-rig deployment.

---

# Strategic framing

v1 commits to one of two possible futures:

- **(A) Stable low-overhead inference-observability runtime** — the chosen direction.
- **(B) Research-grade GPU telemetry lab framework** — explicitly deferred to v2/C.5+.

Mixing the two simultaneously is what caused the v1-as-microkernel risk. v1 is A. v2 may layer B on top once A is stable.

---

# Open questions

Answered from the hardware drop (struck through for reference):

- ~~GPU count + SKUs~~ → **2× Intel Arc Pro B70, 32 GB each (28 GB with ECC). PCIe 5.0 ×16 cards on a PCIe 4.0 host (negotiating down).**
- ~~Per-card VRAM~~ → **32 GB raw / 28 GB ECC-on.**
- ~~Host RAM~~ → **32 GB DDR4.**

Still open:

1. **ECC mode in current operation.** ON or OFF on the cards right now? (Affects which math applies for headroom.)
2. **Inference workload identity.** Known process name to sniff (`python.exe`, llama.cpp, vLLM?), or always passed via `--workload-pid`?
3. **Default cadence.** 1000 ms or 500 ms? Plan defaults to 1000 ms with ±25–50 ms jitter; user opt-in for 500 ms.
4. **JSONL output location.** Default `D:\` (e.g. `D:\work\b70tools\runs\`) or `C:\` (system drive)? Strong recommendation: not C:.
5. **`pdh_gpu_engine_lite` default-on or default-off in v1?** Plan currently default-off pending measured overhead. Concur, or include in default Tier 1?
6. **Where do we start coding?** Recommendation: A.18 step 3 (`identity/`) — first user-facing answer (questions 1 + 2 from A.3) in the smallest possible diff.
