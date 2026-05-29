# b70tools — 2026-05-28 Repo Status

**Generated:** 2026-05-28  
**Rig:** 2× Intel Arc Pro B70 · AMD Ryzen 9 5900X · 32 GB DDR4 · Win 10 Pro 19045  
**Driver:** 32.0.101.8801 (Intel Xe2 / Battlemage production)  
**Build:** `build/b70tools.exe` 490 KB · RelWithDebInfo · MSVC 2022

---

## Table of Contents

1. [What This Is](#1-what-this-is)
2. [Hardware & Driver Context (this rig)](#2-hardware--driver-context-this-rig)
3. [Build Status](#3-build-status)
4. [Run Index](#4-run-index)
   - 4a. [Development Probes (m1–m2x)](#4a-development-probes-m1m2x)
   - 4b. [Baseline & Validation Runs](#4b-baseline--validation-runs)
   - 4c. [Inference Telemetry Runs](#4c-inference-telemetry-runs)
5. [Timing & Performance Comparison](#5-timing--performance-comparison)
   - 5a. [Observation Cost: All Runs](#5a-observation-cost-all-runs)
   - 5b. [Inference Throughput Comparison](#5b-inference-throughput-comparison)
   - 5c. [Thermal Envelopes by Run](#5c-thermal-envelopes-by-run)
   - 5d. [JSONL Output Rate by Run](#5d-jsonl-output-rate-by-run)
6. [Key Findings](#6-key-findings)
7. [Known Issues & Telemetry Limitations](#7-known-issues--telemetry-limitations)
8. [v1.5 Priority Queue](#8-v15-priority-queue)
9. [Documentation Index](#9-documentation-index)
10. [Eval Framework Runs (2026-05-28)](#10-eval-framework-runs-2026-05-28)
11. [Git Setup](#11-git-setup)

---

## 1. What This Is

`b70tools` is a lightweight Windows-native GPU telemetry and observability tool for
profiling multi-GPU Vulkan inference workloads. It produces arbitrated, structured JSONL
telemetry from four passive collectors (D3DKMT, DXGI VMI, Vulkan budget, IGCL), detects
cross-source disagreements, and exposes five CLI analysis verbs.

**v1 implementation is complete.** The tool has been exercised against idle baselines,
single-GPU Mistral 24B inference, concurrent dual-card independent inference, and
dual-card layer-split inference with both MoE and dense models. All experimental
milestones hit as of 2026-05-28.

```
b70tools --enumerate / --dry-run / --run   (capture)
b70tools summarize / adapters / disagreements / self / verdict   (analysis)
```

---

## 2. Hardware & Driver Context (this rig)

| Component | Value | Notes |
|---|---|---|
| GPU 0 | Intel Arc Pro B70 (adapter_00011b4f) | Slot 2 (bottom); **healthy IGCL telemetry** |
| GPU 1 | Intel Arc Pro B70 (adapter_00012fbe) | Slot 1 (top); DP-connected; **broken IGCL voltage/freq** |
| Device ID | 0xE223 (both) | Battlemage Xe2 |
| VRAM | 32 GB GDDR6 raw / 28 GB ECC each | 31.12 GiB budget per card |
| PCIe | Host PCIe 4.0 × 8 per slot (cards are PCIe 5.0 capable) | Downgraded at host |
| CPU | AMD Ryzen 9 5900X (12C/24T) | 3.7 GHz |
| RAM | 32 GB DDR4 (~20.7 GB available at measurement time) | |
| OS | Windows 10 Pro 10.0.19045 | WDAC Enforced, Secure Boot On, KDMA On, Hyper-V Off |
| Driver | 32.0.101.8801 | Production Intel Arc driver as of 2026-05-28 |
| RivaTuner | RTSSVkLayer64.dll intercepts Vulkan | Captured by audit; doesn't affect compute |
| Physical layout | adapter_00012fbe is cramped — top slot, sandwiched between CPU heatsink and card below | Runs ~10–12 °C hotter VRAM at load than bottom card |

**ReBAR:** active on both cards. **ECC:** currently OFF. **WDAC self-detect bug:** reports `false` when actually Enforced.

---

## 3. Build Status

```
Compiler:    MSVC 2022 Community (cl.exe)
Standard:    C++20, /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc
Config:      RelWithDebInfo
Output:      build\b70tools.exe  (490 KB)
Build date:  2026-05-28 09:23 UTC
Linked:      dxgi dxguid setupapi cfgmgr32 pdh (no Vulkan .lib — VK_NO_PROTOTYPES)
Third-party: Vulkan-Headers (headers-only), IGCL (headers + sample stubs)
```

Build with:
```powershell
.\build.ps1
```
Or directly:
```powershell
.\configure-and-build.cmd
```

---

## 4. Run Index

### 4a. Development Probes (m1–m2x)

Early per-collector smoke tests used during implementation. Not documented in findings;
preserved in `runs/` for debugging. Not included in `.gitignore` exclusion but not
considered operational results.

| Run | Time (PDT) | Size | Purpose |
|---|---|---|---|
| `runs/m1/` | 02:08 | 10.5 KB | First collector init smoke test |
| `runs/m2/` | 02:33 | 8.4 KB | DXGI+D3DKMT basic read |
| `runs/m2b/` | 02:36 | 8.4 KB | DXGI variation |
| `runs/m2c/` | 02:37 | 9.0 KB | DXGI variation |
| `runs/m2-fp/` | 02:46 | 13.1 KB | Driver fingerprint integration |
| `runs/m2-fp2/` | 02:48 | 11.4 KB | Driver fingerprint tuning |
| `runs/m2-igcl/` | 02:58 | 18.9 KB | IGCL collector integration |
| `runs/enum1/` | 02:21 | 1.4 KB | `--enumerate` identity reconciliation test |

Raw JSONL: `runs/<name>/events.jsonl`

---

### 4b. Baseline & Validation Runs

| Run | Time (PDT) | Duration | JSONL | Purpose |
|---|---|---|---|---|
| `runs/rules-verify/` | 03:54 | short | 21 KB | Disagreement rules unit validation |
| `runs/preflight/` | 04:13 | 1 tick | 1.4 KB | Pre-experiment adapter enumeration |
| `runs/baseline-idle-1/` | 03:55–04:00 | **304 s (300 ticks)** | **324 KB** | **Noise floor reference — see [Finding docs](#9-documentation-index)** |
| `runs/silence-rule-validation-1/` | 05:25 | ~240 s | 225 KB | Silent-IGCL rule validation (rule should NOT fire at idle) |
| `runs/d3dkmt_qs_smoke/` | 08:56 | short | 29 KB | D3DKMT QueryStatistics collector smoke (returns INVALID_PARAMETER on Win10 19045 — expected) |
| `runs/host_mem_smoke/` | 08:59 | short | 21 KB | Host memory collector smoke |
| `runs/pdh_verdict_smoke/` | 09:05 | short | 45 KB | PDH + verdict verb smoke |

Raw JSONL: `runs/<name>/events.jsonl`

---

### 4c. Inference Telemetry Runs

These are the operational inference experiments. See linked findings docs for full analysis.

| Run | Time (PDT) | Duration | JSONL | Model / Workload | Cards | Gen t/s | Findings |
|---|---|---|---|---|---|---|---|
| `runs/single-gpu-mistral24b-1/` | 04:29 | **182 s** | **243 KB** | Mistral-Small-3.2-24B Q4_K_M | Vulkan0 (card 0) | 27.3 | [findings-single-gpu-mistral24b-1.md](docs/findings-single-gpu-mistral24b-1.md) |
| `runs/single-gpu-mistral24b-2/` | 04:39 | **182 s** | **355 KB** | Mistral-Small-3.2-24B Q4_K_M | Vulkan1 (card 1) | 27.8 | [findings-single-gpu-both-baselines.md](docs/findings-single-gpu-both-baselines.md) |
| `runs/both-cards-concurrent-mistral24b-1/` | 05:12 | **242 s** | **255 KB** | Mistral 24B Q4 × 2 independent | Both (independent) | 27.3 + 28.2 | [findings-both-cards-concurrent-mistral24b-1.md](docs/findings-both-cards-concurrent-mistral24b-1.md) |
| `runs/both-cards-concurrent-mistral24b-2/` | 05:36 | ~360 s | **476 KB** | Mistral 24B Q4 × 2 (silence rule validation) | Both (independent) | — | silence-rule-validation |
| `runs/dual-b70-qwen30b-moe-1/` | 05:48 | **605 s** | **1.15 MB** | Qwen3-30B-A3B MoE Q4_K_M | Both (layer split) | **81.7** | [findings-dual-b70-qwen30b-moe-1.md](docs/findings-dual-b70-qwen30b-moe-1.md) |
| `runs/dual-b70-qwen25-32b-q4-1/` | 06:28 | **606 s** | **1.16 MB** | Qwen2.5-32B dense Q4_K_M | Both (layer split) | **20.7** | [findings-dual-b70-qwen25-32b-q4-1.md](docs/findings-dual-b70-qwen25-32b-q4-1.md) |

To open a raw log in your editor:
```powershell
Start-Process "D:\work\b70tools\runs\dual-b70-qwen30b-moe-1\events.jsonl"
```

To re-analyze any run:
```powershell
& "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" summarize     "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" verdict       "D:\work\b70tools\runs\<name>"
```

---

## 5. Timing & Performance Comparison

All measurements from `b70tools self` and `b70tools adapters` on the rig described in §2.
Driver: 32.0.101.8801. Build: State B (open workbench, improved airflow, no case).

### 5a. Observation Cost: All Runs

Delta-suppressed JSONL writes. Do-no-harm budget: <50 MiB RSS target, <30 MiB stretch.

```
Run                              | Window  | RSS     | Init wall | B/s    | Pass?
---------------------------------|---------|---------|-----------|--------|------
baseline-idle-1                  | 304 s   | 16.6 MB | 126.8 ms  | 1090   | ✓ PASS
single-gpu-mistral24b-1 (Vk0)   | 182 s   | 16.6 MB | 117.8 ms  | 1361   | ✓ PASS
single-gpu-mistral24b-2 (Vk1)   | 182 s   | 16.7 MB | ~120 ms   | 1992   | ✓ PASS
both-cards-concurrent-1         | 242 s   | 16.6 MB | 377.3 ms* | 1076   | ✓ PASS
dual-b70-qwen30b-moe-1           | 605 s   | 16.7 MB | 125.9 ms  | 1983   | ✓ PASS
dual-b70-qwen25-32b-q4-1         | 606 s   | 16.6 MB | 51.6 ms   | 1953   | ✓ PASS
```

`*` Concurrent run: 304.5 ms Vulkan init due to ICD serialization contention (both
llama-cli processes initializing simultaneously). Resolved by starting b70tools 12 s
before the workload in all subsequent runs.

**Collector init breakdown (baseline-idle-1):**

```
d3dkmt_adapter_perfdata:    26.9 ms   +8 KiB RSS   0 modules added   (TrulyPassive)
dxgi_query_video_memory:    23.4 ms   +4 KiB RSS   0 modules added   (TrulyPassive)
vulkan_memory_budget:       51.5 ms   +16.6 MB RSS  15 modules added  (DriverPassive — ICD load)
igcl_power_telemetry:       25.0 ms   +8 KiB RSS   0 modules added   (DriverPassive — DLLs already loaded)
```

The 16.6 MB RSS is 100% the Vulkan ICD loader. All other collectors cost KiB.

---

### 5b. Inference Throughput Comparison

Measured by llama-cli stdout on this rig (dual-B70, driver 32.0.101.8801).

```
Workload                         | Config      | Prompt eval | Gen t/s | VRAM/card
---------------------------------|-------------|-------------|---------|----------
Mistral-Small-3.2-24B Q4_K_M    | Single Vk0  | ~400 t/s pp | 27.3    | ~14 GB (1 card)
Mistral-Small-3.2-24B Q4_K_M    | Single Vk1  | ~443 t/s pp | 27.8    | ~14 GB (1 card)
Mistral-Small-3.2-24B Q4_K_M    | Concurrent  | 428.9 / 443.1 pp | 27.3 + 28.2 | ~14 GB each
Qwen3-30B-A3B MoE Q4_K_M        | Layer split | 30.1 t/s pp  | 81.7   | ~8.5 GB each
Qwen2.5-32B dense Q4_K_M        | Layer split | 242.2 t/s pp | 20.7   | ~9.3 GB each
```

**MoE vs dense (same precision, same dual-B70 layer split):**

```
Metric              | Qwen3-30B-A3B MoE | Qwen2.5-32B dense | Ratio
--------------------|-------------------|-------------------|-------
Prompt eval t/s     |          30.1     |         242.2     |  8.0× faster dense
Generation t/s      |          81.7     |          20.7     |  4.0× faster MoE
Die temp peak       |          61 °C    |          66 °C    | +5 °C on dense
VRAM temp peak (Vk1)|          64 °C    |          74 °C    | +10 °C on dense
Fan peak            |        1083 RPM   |        1370 RPM   | +27% on dense
Activity rate       |           3.3%    |           5.7%    | +73% on dense
Freq mean           |       1.967 GHz   |       2.250 GHz   | +14% sustained
```

**Recommendation:** MoE (Qwen3-30B-A3B) for fast iterative/chat (~82 t/s gen);
dense (Qwen2.5-32B) for sustained structured output or deep analysis (242 t/s prompt eval).

---

### 5c. Thermal Envelopes by Run

`adapter_00011b4f` = healthy IGCL (bottom slot, Vulkan0).
`adapter_00012fbe` = broken IGCL (top slot, cramped, Vulkan1).
Build state A = open workbench, no forced airflow. State B = improved airflow tweak.

```
Run                          | State | Card 0 die | Card 0 VRAM | Card 1 die | Card 1 VRAM
-----------------------------|-------|-----------|-------------|-----------|-------------
baseline-idle-1              |  A    |    49 °C  |    52 °C    |    55 °C  |    62 °C
single-gpu-mistral24b-1 (0)  |  A    |    73 °C  |    78 °C    |    55 °C* |    62 °C*
single-gpu-mistral24b-2 (1)  |  A    |    49 °C* |    52 °C*   |    74 °C  |    90 °C  ← peak
both-cards-concurrent-1      |  B    |    73 °C  |    78 °C    |     n/a†  |     n/a†
dual-b70-qwen30b-moe-1       |  B    |    61 °C  |    60 °C    |    61 °C  |    64 °C
dual-b70-qwen25-32b-q4-1     |  B    |    66 °C  |    68 °C    |    67 °C  |    74 °C
```

`*` Card was idle in this run — values are idle baseline, not workload-driven.  
`†` IGCL went completely silent on card 1 during concurrent Vulkan init — no thermal samples emitted.

**Takeaways:**
- State A solo on card 1: VRAM hit 90 °C — highest measured, still safe (GDDR6 spec ~105 °C) but a thermal concern for sustained runs.
- State B airflow tweak reduced card 1 VRAM from 90 °C (State A solo) to 64–74 °C (State B dual). ~16–26 °C improvement.
- Both cards hit identical 61 °C die peaks on MoE dual-split — strongest evidence the layer split engaged both equally.
- Slot 1 (card 1, cramped) runs 4–10 °C hotter VRAM than slot 2 under any load — structural from PCIe layout.

---

### 5d. JSONL Output Rate by Run

Delta-suppression means only changed metrics are written. Rates scale with metric churn
(more churn during inference, and when IGCL is firing disagreements frequently).

```
Run                          | Events | Size   | B/s    | B/event
-----------------------------|--------|--------|--------|--------
baseline-idle-1              |  1376  | 324 KB | 1090   | 240.9
single-gpu-mistral24b-1      |  1038  | 242 KB | 1361   | 239.2
single-gpu-mistral24b-2      |  1500  | 355 KB | 1992   | 241.8
both-cards-concurrent-1      |  1093  | 255 KB | 1076   | 238.7  ← IGCL silence suppressed volume
dual-b70-qwen30b-moe-1       |  4750  | 1.15 MB| 1983   | 252.9
dual-b70-qwen25-32b-q4-1     |  4862  | 1.16 MB| 1953   | 238.7 *approx
```

At 2 KiB/s, a 10-hour continuous run would produce ~70 MB. Sustainable on any SSD.

---

## 6. Key Findings

### F1 — Cross-API identity reconciliation works

DXGI / Vulkan / SetupAPI / IGCL adapters all bound to the same LUID. Both cards
correctly identified, named, and distinguished across all runs.

### F2 — Signal-to-noise is excellent

Single-card inference vs. idle:
- Activity rate: **10× increase** (32% vs 3%)
- Clock: **5× increase** (2.8 GHz vs 550 MHz)
- Temperature: **+24 °C die, +26 °C VRAM**

These signals cleanly distinguish "card working" from "card idle" with no ambiguity.

### F3 — Both Arc Pro B70s perform identically in compute

Solo Mistral 24B inference on each card independently:
- Card 0: 27.3 t/s generation
- Card 1: 27.8 t/s generation

**The IGCL telemetry path on card 1 is broken; the GPU hardware is fully healthy.**
"Broken card" should be replaced with "broken-telemetry card" in all future discussion.

### F4 — Noise floor stable across all workload classes

Across 6 operational inference runs, the same 3 disagreement classes appeared:

```
1. expected_source_unavailable  /  adapter_00011b4f      (D3DKMT returns INVALID_PARAMETER)
2. physically_impossible_voltage / adapter_00012fbe      (IGCL reads 5.117 V — bogus)
3. physically_impossible_frequency / adapter_00012fbe    (IGCL reads 8.550 GHz — bogus)
```

No new classes. Any future run that produces a 4th class is a real finding.

### F5 — IGCL on card 1 goes completely silent under concurrent Vulkan init

When both cards receive simultaneous Vulkan workloads, `ctlPowerTelemetryGet` for card 1
returns non-success and the collector silently drops all samples. Zero thermal/clock/voltage
emitted during `both-cards-concurrent-mistral24b-1`. **New in v1.5: `previously_reporting_source_went_silent` disagreement rule** to detect this. Validated — did NOT fire on the subsequent MoE and dense layer-split runs (rule works, has no false positives).

### F6 — State B airflow provides ~20 °C VRAM cooling on the cramped card

Card 1 (slot 1, cramped) went from 90 °C VRAM (State A solo Mistral) to 64–74 °C (State B
dual layer-split). Dominant effect is the airflow improvement; the halved per-card load from
layer-split is a secondary contributor. Closed-case (State C) would likely improve further.

### F7 — MoE vs dense performance profiles are clearly distinct

Same dual-B70 setup, same precision (Q4_K_M):
- **MoE (Qwen3-30B-A3B):** 82 t/s gen, 30 t/s prompt eval — fast chat
- **Dense (Qwen2.5-32B):** 21 t/s gen, 242 t/s prompt eval — fast structured output

### F8 — Do-no-harm budget held across every run

RSS: 16.6–16.7 MiB across idle, single-GPU, concurrent, and dual-split runs.
Target: <50 MiB. Stretch: <30 MiB. **Both passed in every run. Zero watchdog kicks.**

---

## 7. Known Issues & Telemetry Limitations

| Issue | Scope | Status |
|---|---|---|
| D3DKMT `ADAPTERPERFDATA` returns `INVALID_PARAMETER` on Win10 19045 | System-level | Expected; rule fires once per session. v1.5 has fix-or-fallback in plan. |
| IGCL voltage/frequency on adapter_00012fbe reads wrong registers (5.117 V / 8.55 GHz) | Card 1 only | Structural; flagged by `physically_impossible_*` rules every heartbeat. Workaround: ignore those fields on that card; temperature is credible. |
| IGCL activity counters on adapter_00012fbe run ~94× wall clock at idle, ~45× under load | Card 1 only | Structural; same root cause as voltage/freq. Counter values useless for card 1. |
| IGCL card 1 goes completely silent under concurrent Vulkan init | Card 1 only | New in concurrent-1 run. `previously_reporting_source_went_silent` rule added. No false positives in 3 subsequent runs. |
| **v1 cannot see workload VRAM residency** | All runs | DXGI VMI and Vulkan budget are per-process — b70tools sees 4 KiB (its own allocs). 14–18 GB of model weights are invisible. **v1.5 priority #1: PDH `GPU Process Memory` full set.** |
| WDAC self-detect reports `false` when actually Enforced | Driver fingerprint | Cosmetic; doesn't affect telemetry signals. |
| RivaTuner `RTSSVkLayer64.dll` intercepts Vulkan | All runs | Expected; captured by audit. Doesn't affect compute or telemetry. |
| `card.energy_j_counter` stuck at 0 on adapter_00011b4f | Card 0 only | Minor; all other card 0 metrics are healthy. |
| PCIe link gen/width from SetupAPI devnode is unreliable | Both cards | Not emitted as MetricSamples; deferred. |

---

## 8. v1.5 Priority Queue

Ordered by operational impact on this rig:

1. **PDH `GPU Process Memory` full set** — removes the VRAM blindspot. Without this, b70tools cannot answer "how much VRAM is the workload using?" The collector family is already proven safe (same PDH path as working engine counters).
2. **`previously_reporting_source_went_silent` general rule** — already shipped a targeted IGCL version; generalize to cover any `(source, adapter, metric)` tuple that stops emitting after N ticks.
3. **`Idle → Awake → ActiveCompute` FSM transitions** — v1 FSM only handles `Unknown → Idle`. Adding the full transition chain enables the `adapters` verb to report state without manual counter-delta analysis.
4. **Replay fixtures** — static JSONL files as regression test inputs; needed before adding more disagreeement rules.
5. **`b70tools compare`** — fingerprint + per-adapter metric diff across two runs. Would make idle-vs-inference and card0-vs-card1 comparisons one-liner.

---

## 9. Documentation Index

| File | Content |
|---|---|
| `docs/plan.md` | Full architecture specification: v1, v1.5, v2 |
| `docs/operational-runbook.md` | How to conduct experiments (preflight, idle, single-GPU, dual-B70) |
| `docs/cli-ui-plan.md` | CLI verb design rationale |
| `docs/baseline-findings-idle.md` | Idle noise floor characterization (304 s, 300 ticks) |
| `docs/findings-single-gpu-mistral24b-1.md` | Single-card inference telemetry findings |
| `docs/findings-single-gpu-both-baselines.md` | Per-card solo baseline comparison (card 0 vs card 1) |
| `docs/findings-both-cards-concurrent-mistral24b-1.md` | Concurrent dual-card independent inference |
| `docs/findings-dual-b70-qwen30b-moe-1.md` | Dual-card layer-split MoE inference |
| `docs/findings-dual-b70-qwen25-32b-q4-1.md` | Dual-card layer-split dense inference + MoE vs dense comparison |
| `docs/runbook-fresh-b70-pc.md` | **Getting started on a fresh PC with a new Arc Pro B70** |
| `eval/README.md` | AI evaluation framework overview |
| `eval/benchmark-overview.md` | Benchmark methodology |

Open a doc:
```powershell
Start-Process "D:\work\b70tools\docs\operational-runbook.md"
```

---

## 10. Eval Framework Runs (2026-05-28)

The `eval/` directory contains a separate automated model evaluation framework. These runs
test AI models' ability to reason about the repo's own artifacts. They are parallel to
(not part of) the b70tools telemetry runs in §4.

All eval runs use dual-B70 layer split (`-sm layer -ts 1,1 -fit off`) with
`GGML_VK_VISIBLE_DEVICES=0,1 GGML_VK_DISABLE_COOPMAT=1`.

| Run Name | Started (PDT) | Model | Mode | ctx | max_tok |
|---|---|---|---|---|---|
| `smoke-q4-1r1p200t` | 09:10 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 32k | 200 |
| `smoke-q4-c65k-1r1p200t` | 09:13 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 65k | 200 |
| `smoke-q4-utf8-1r1p200t` | 09:15 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 65k | 200 |
| `smoke-q4-yarn-1r1p200t` | 09:17 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 65k | 200 |
| `smoke-q4-yarn2-1r1p200t` | 09:18 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 65k | 200 |
| `smoke-q4-32k-1r1p200t` | 09:20 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 32k | 200 |
| `smoke-q4-relaxed-1r1p200t` | 09:24 | Qwen2.5-32B-Instruct Q4_K_M | stateless | 32k | 200 |
| `qwen2.5-coder-32b-instruct-q5_k_m-20260528-092819` | 09:28 | Qwen2.5-Coder-32B Q5_K_M | stateless | 32k | 1500 |
| `qwen2.5-32b-instruct-q6_K-20260528-092819` | 11:17 | Qwen2.5-32B-Instruct Q6_K | stateless | 32k | 1500 |
| `baselines-20260528-092819` | (group) | Qwen2.5-32B Q6_K + Coder Q5 | stateless | 32k | 1500 |
| `smoke-stateful-q4-1r1p200t` | 13:26 | Qwen2.5-32B Q4_K_M | **stateful** | 32k | 200 |
| `stateful-qwen2.5-coder-32b-instruct-q5_k_m-20260528-133236` | 13:32 | Qwen2.5-Coder-32B Q5_K_M | **stateful** | 32k | 1500 |
| `stateful-qwen2.5-32b-instruct-q6_K-20260528-133236` | 14:54 | Qwen2.5-32B Q6_K | **stateful** | 32k | 1500 |
| `stateful-baselines-20260528-133236` | (group) | Q6_K + Coder Q5 | **stateful** | 32k | 1500 |
| `Qwen3-30B-A3B-Instruct-2507-Q4_K_M-20260528-155458` | 15:55 | Qwen3-30B-A3B MoE Q4_K_M | stateless | 32k | 1500 |
| `baselines-20260528-155458` | (group) | Qwen3-30B MoE + 32B dense | stateless | 32k | 1500 |
| `stateful-Qwen3-30B-A3B-Instruct-2507-Q4_K_M-20260528-164027` | 16:40 | Qwen3-30B MoE Q4_K_M | **stateful** | 32k | 1500 |
| `stateful-baselines-20260528-164027` | (group) | (baselines) | **stateful** | 32k | 1500 |

Each run contains: `manifest.json`, `telemetry/events.jsonl`, `verdicts/`, `server/`, `snapshot.md`.

Raw log for a run:
```powershell
explorer.exe "D:\work\b70tools\eval\runs\Qwen3-30B-A3B-Instruct-2507-Q4_K_M-20260528-155458"
```

---

## 11. Git Setup

The repo directory has a `.gitignore` already configured. It excludes:
`build/`, `out/`, `runs/`, `*.obj`, `*.exe`, `*.pdb`, `*.ilk`, `*.idb`, `.vs/`, `.vscode/`

The `eval/runs/` directory and `eval/snapshots/` are **not** in `.gitignore` — decide
whether to include them (they contain model outputs and context snapshots, can be large).

To initialize git and make the first commit:

```powershell
cd D:\work\b70tools
git init
git add .
git status    # review what's staged — verify eval/runs/ size before committing
git commit -m "b70tools v1: passive observer, 4 collectors, 5 analysis verbs, 6+ operational runs"
```

To push to GitHub (create a repo on github.com first, then):
```powershell
git remote add origin https://github.com/<your-user>/b70tools.git
git branch -M main
git push -u origin main
```

**Note:** `runs/` (telemetry JSONL) is excluded from git by `.gitignore`. If you want to
preserve the raw telemetry separately, consider a dedicated storage location or a second
repo. The `eval/runs/` directory is currently not excluded — review its size before pushing.
