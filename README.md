# b70tools

A lightweight Windows-native GPU telemetry and observability tool for multi-GPU Vulkan
inference on Intel Arc Pro B70 hardware. Built to answer one question:

> **When you run a model split across two 32 GB Arc Pro B70 cards on Windows, what is
> actually happening — and is Task Manager telling you the truth?**

It wasn't. This tool was built to find out what the real story is.

---

## The Problem

Running large Vulkan inference workloads across two Arc Pro B70s on Windows produces
confusing telemetry. Task Manager's GPU% column shows one card working, the other idle —
even when both are clearly warm. The Arc Control panel gives different numbers than Task
Manager. `ctlPowerTelemetryGet` (Intel's IGCL API) returns values that are physically
impossible on one of the two cards. And the worst case — cold-starting a dual-card split
near the VRAM ceiling — can cascade into a near-total system lockup: GPU fans spinning
full, UI frozen, one card showing zero activity in perf monitor, the other pegged at 100%.
Recovery sometimes requires a BIOS reflash.

`b70tools` is a passive observer built to arbitrate between these conflicting sources and
produce structured, interpretable telemetry without perturbing the system it measures.

---

## Hardware

| | |
|---|---|
| **GPUs** | 2× Intel Arc Pro B70 (Battlemage Xe2, 0xE223, 32 GB GDDR6 each) |
| **CPU** | AMD Ryzen 9 5900X |
| **RAM** | 32 GB DDR4 |
| **OS** | Windows 10 Pro 10.0.19045 |
| **Driver** | 32.0.101.8801 (Intel Arc, production) |
| **PCIe** | 4.0 ×8/×8 (cards are PCIe 5.0 capable; host limits to 4.0) |

The two cards are physically different. The **top-slot card** (Vulkan1) is wedged between
the CPU heatsink and the second card, DisplayPort-connected, and runs 10–15 °C hotter
VRAM under identical load. Its IGCL telemetry path also reads the wrong hardware
registers — 5.117 V and 8.55 GHz at idle, which are physically impossible. The
**bottom-slot card** (Vulkan0) has clean IGCL telemetry and better thermals.

Both cards deliver **identical inference throughput**. The broken telemetry is a driver
issue, not a hardware issue. This took several experiments to confirm definitively.

---

## What Was Built

A single Windows executable (`b70tools.exe`, ~490 KB, zero DLL dependencies) with two
operating modes:

**Capture** — runs a poll loop at 1 Hz, collecting from four passive sources:
- D3DKMT kernel-mode adapter perf counters
- DXGI VideoMemoryInfo (per-process heap budgets)
- Vulkan VK_EXT_memory_budget
- IGCL (Intel GPU Command List) power/thermal/clock telemetry

All samples are written to a delta-suppressed JSONL event log. Cross-source disagreements
are detected and tagged in real time.

**Analysis** — five verbs for interpreting a captured run:
```
b70tools adapters      <run>   — per-adapter activity rates, thermal/clock envelopes
b70tools summarize     <run>   — structured headline report
b70tools disagreements <run>   — cross-source conflict report
b70tools self          <run>   — observation cost (RSS, init time, do-no-harm check)
b70tools verdict       <run>   — aggregate validity verdict (exit 0/2/3)
```

**Observation cost:** 16.6 MiB RSS (all from the Vulkan ICD loader), <130 ms init,
~1–2 KiB/s JSONL write rate. These numbers held stable across every run. The tool does
not measurably perturb the workloads it observes.

---

## The Journey

All experiments were run on 2026-05-28. They proceeded in order, each one building on
the last.

### Idle Baseline — characterizing the noise floor

**[`docs/baseline-findings-idle.md`](docs/baseline-findings-idle.md)**  
**Raw log:** `runs/baseline-idle-1/events.jsonl` (304 s, 324 KB)

Five minutes at idle, no inference running. Goal: understand what the rig looks like
when nothing is happening, so inference deltas are interpretable.

Key finding: the top-slot card's IGCL telemetry is broken in **three distinct categories
simultaneously** — voltage, frequency, and activity counters all reporting impossible
values. This confirmed early that "the card is broken" framing was wrong: the telemetry
path is broken, not the GPU. The idle noise floor settled at exactly 3 disagreement
classes that would persist unchanged across every subsequent run.

---

### Single-GPU Inference — establishing the wake signal

**[`docs/findings-single-gpu-mistral24b-1.md`](docs/findings-single-gpu-mistral24b-1.md)**  
**Raw log:** `runs/single-gpu-mistral24b-1/events.jsonl` (182 s, 243 KB)

Mistral-Small-3.2-24B Q4_K_M, bottom-slot card only (`GGML_VK_VISIBLE_DEVICES=0`).

The first successful operational telemetry capture of real Vulkan inference on this rig.
The active card woke cleanly; the idle card stayed at the noise floor; b70tools itself
did not perturb anything detectable.

Signal-to-noise on the used card:
- Activity rate: **10× increase** (32% vs 3% idle)
- Clock: **5× increase** (2.8 GHz vs 550 MHz)
- Temperature: **+24 °C die, +26 °C VRAM**

The "60–95% activity" expectation from the runbook was wrong. 32% is the real signature
of single-card 24B Q4 Vulkan inference on B70. The runbook was updated.

v1 gap confirmed: b70tools cannot see the 14 GB of model weights loaded onto the card.
DXGI VMI and Vulkan budget are per-process; our process sees 4 KiB (its own allocs).

---

### Per-Card Baselines — proving both GPUs work

**[`docs/findings-single-gpu-both-baselines.md`](docs/findings-single-gpu-both-baselines.md)**  
**Raw logs:** `runs/single-gpu-mistral24b-1/` and `runs/single-gpu-mistral24b-2/`

The same workload run separately on each card. Top-slot card: 27.8 t/s generation.
Bottom-slot card: 27.3 t/s generation. Identical to within noise.

This is the key result that retired the "broken card" framing. **The GPU hardware is
fully healthy. Only the IGCL telemetry path is broken.** The thermal sensors on the
broken-telemetry card are credible (matched expected physical behavior and operator
observation). Voltage/frequency/activity counters are not.

The top-slot card's VRAM hit **90 °C** under solo Mistral 24B in the open-case
workbench setup — highest measured, safe for GDDR6 (spec ~105 °C) but worth watching
under sustained load. This is purely a physical thermal environment issue, not GPU
degradation.

---

### Concurrent Dual-Card — new failure mode discovered

**[`docs/findings-both-cards-concurrent-mistral24b-1.md`](docs/findings-both-cards-concurrent-mistral24b-1.md)**  
**Raw log:** `runs/both-cards-concurrent-mistral24b-1/events.jsonl` (242 s, 255 KB)

Two independent `llama-cli` processes, one per card, running the same workload
simultaneously. Both completed their 1500-token generations. Both delivered ~27–28 t/s.

New finding: under concurrent Vulkan initialization, `ctlPowerTelemetryGet` for the
top-slot card returned non-success for the entire 4-minute window. **Zero thermal, zero
clock, zero voltage samples emitted for that card.** The disagreement count dropped from
25 (solo) to 1 — not because things improved, but because the broken source went silent.

A clean disagreement report can mean healthy telemetry or silently failing telemetry.
This motivated the `previously_reporting_source_went_silent` rule added to v1.5.

Also found: Vulkan init time jumped from ~50 ms to 304 ms (6.1×) due to ICD serialization
contention. The audit framework caught this honestly.

---

### Dual-Card Layer Split, MoE — the milestone

**[`docs/findings-dual-b70-qwen30b-moe-1.md`](docs/findings-dual-b70-qwen30b-moe-1.md)**  
**Raw log:** `runs/dual-b70-qwen30b-moe-1/events.jsonl` (605 s, 1.15 MB)

Qwen3-30B-A3B (MoE, 3B active params/token, Q4_K_M) via `llama-cli` with
`-sm layer -ts 1,1 -fit off`. b70tools started 12 s before the workload.

**This is the Experiment 3 milestone** — the first clean dual-B70 layer-split telemetry
capture. Both cards hit identical 61 °C die peaks. Generation throughput: **81.7 t/s**
(vs 27 t/s single-card). The silence cascade that had caused system lockups in the
operator's prior experience did not trigger.

The `-fit off` flag turned out to be critical. Without it, llama.cpp's auto-fit routes
all layers to one card. With it, the 50/50 split is enforced. The temperature signal —
both cards heating equally — is the clearest confirmation the split is real.

The IGCL silence rule did not fire. Validated: 0 false positives across ~25 minutes of
cumulative healthy operation across this and subsequent runs.

---

### Dual-Card Layer Split, Dense — MoE vs dense comparison

**[`docs/findings-dual-b70-qwen25-32b-q4-1.md`](docs/findings-dual-b70-qwen25-32b-q4-1.md)**  
**Raw log:** `runs/dual-b70-qwen25-32b-q4-1/events.jsonl` (606 s, 1.16 MB)

Qwen2.5-32B-Instruct Q4_K_M, same dual-split setup. This is the dense model comparison
to the prior MoE experiment.

| | Qwen3-30B MoE | Qwen2.5-32B dense |
|---|---|---|
| Prompt eval | 30.1 t/s | **242.2 t/s** (8× faster) |
| Generation | **81.7 t/s** | 20.7 t/s |
| Die temp peak | 61 °C | 66 °C |
| VRAM temp peak (top card) | 64 °C | 74 °C |
| Fan peak | 1083 RPM | 1370 RPM |

**Use MoE for fast iterative work; dense for structured output or long-context analysis.**
The "+5–10 °C across the board" thermal delta on dense is the telemetry signature of
sustained full-parameter compute vs sparse-activation inference.

---

## Performance Reference (this rig, driver 32.0.101.8801)

```
Workload                          Config        Gen t/s   Prompt t/s   VRAM/card
Mistral-Small-3.2-24B Q4_K_M     Single card   27         ~400–443     14 GB
Qwen3-30B-A3B MoE Q4_K_M         Dual split    81.7       30.1         8.5 GB
Qwen2.5-32B-Instruct Q4_K_M      Dual split    20.7       242.2        9.3 GB
```

Observation cost across all runs: **16.6 MiB RSS, <130 ms init, 0 watchdog kicks.**

---

## Known Rig-Specific Issues

These are stable, characterized, and filed in the noise floor. They do not affect
inference quality — only what b70tools can credibly report about it.

| Issue | Card | Impact |
|---|---|---|
| IGCL voltage/frequency reads wrong registers (5.117 V / 8.55 GHz at idle) | Top slot (adapter_00012fbe) | Voltage and freq telemetry for this card is unusable; temperature is credible |
| IGCL activity counters run ~45–94× wall clock | Top slot | Activity counters for this card are unusable |
| IGCL may go completely silent under concurrent Vulkan init | Top slot | Zero telemetry during contention window; `previously_reporting_source_went_silent` rule fires if this recurs |
| D3DKMT `ADAPTERPERFDATA` returns INVALID_PARAMETER | Both | Expected on Win10 19045; flagged once per session |
| Cannot see workload VRAM residency | Both | DXGI/Vulkan budget are per-process; 14–18 GB of model weights are invisible to v1 |

The VRAM blindspot is the most operationally significant. v1.5's PDH `GPU Process Memory`
collector is the fix.

---

## Status

**v1 is complete.** All planned experiments have been run. The architecture is stable,
the noise floor is characterized, and the do-no-harm budget held across every run.

See [`b70tools_5_28_repo_status.md`](b70tools_5_28_repo_status.md) for the full run
index with raw log links, timing charts, and comparison tables.

**v1.5 priorities:**
1. PDH `GPU Process Memory` — removes the VRAM blindspot
2. `Idle → Awake → ActiveCompute` FSM transitions
3. Generalized `previously_reporting_source_went_silent` rule
4. Replay fixtures for regression testing

---

## Getting Started

If you have an Arc Pro B70 and want to run this yourself:

→ **[`docs/runbook-fresh-b70-pc.md`](docs/runbook-fresh-b70-pc.md)** — complete setup
guide from unboxing to first telemetry capture, including driver gotchas, llama.cpp
Vulkan setup, build instructions, and thermal management tips.

Quick build:
```powershell
git clone https://github.com/djcdevelopment/b70tools.git
cd b70tools
.\build.ps1
.\build\b70tools.exe --enumerate --out .\runs\preflight
```

Requires Visual Studio 2022 Community with the "Desktop development with C++" workload.

---

## Repo Layout

```
src/               C++ source (collectors, arbitrator, schema, identity, runtime, tools)
docs/              Findings docs and runbooks
eval/              Automated model evaluation framework + scripts
third_party/       Vulkan-Headers (Khronos), IGCL (Intel)
build.ps1          Build script (locates MSVC, runs CMake + Ninja)
CMakeLists.txt     Build configuration
b70tools_5_28_repo_status.md   Full run index + timing charts + status snapshot
```

Raw telemetry logs (`runs/`) are excluded from git — they're large JSONL files and
regenerable. The findings docs summarize everything that matters from each run, with
re-run instructions so any experiment can be reproduced.
