# b70tools

A lightweight Windows-native GPU telemetry and observability tool for multi-GPU Vulkan
inference on Intel Arc Pro B70 hardware. Built to answer one question:

> **When you run a model split across two 32 GB Arc Pro B70 cards on Windows, what is
> actually happening — and is Task Manager telling you the truth?**

It wasn't. This tool was built to find out what the real story is.

---

## Table of Contents

- [Quick Start](#quick-start)
- [The Problem](#the-problem)
- [Hardware](#hardware)
- [What Was Built](#what-was-built)
- [The Journey](#the-journey)
  - [Stage 1: Idle Baseline](#stage-1-idle-baseline)
  - [Stage 2: Single-GPU Inference](#stage-2-single-gpu-inference)
  - [Stage 3: Per-Card Baselines](#stage-3-per-card-baselines)
  - [Stage 4: Concurrent Dual-Card](#stage-4-concurrent-dual-card)
  - [Stage 5: Dual-Card Layer Split (MoE)](#stage-5-dual-card-layer-split-moe)
  - [Stage 6: Dual-Card Layer Split (Dense)](#stage-6-dual-card-layer-split-dense)
- [Performance Reference](#performance-reference)
- [Known Rig-Specific Issues](#known-rig-specific-issues)
- [Status and Whats Next](#status-and-whats-next)
- [Repo Layout](#repo-layout)

---

## Quick Start

**Prerequisites:** Visual Studio 2022 Community with the "Desktop development with C++"
workload. That's it — no other tools required.

```powershell
git clone https://github.com/djcdevelopment/b70tools.git
cd b70tools
.\build.ps1                  # locates MSVC, runs CMake + Ninja → build\b70tools.exe
```

Verify both cards are recognized:
```powershell
.\build\b70tools.exe --enumerate --out .\runs\preflight
.\build\b70tools.exe summarize .\runs\preflight
```

Capture a 5-minute idle baseline:
```powershell
.\build\b70tools.exe run --ticks 300 --out .\runs\baseline-idle-1
.\build\b70tools.exe adapters      .\runs\baseline-idle-1
.\build\b70tools.exe disagreements .\runs\baseline-idle-1
.\build\b70tools.exe self          .\runs\baseline-idle-1
```

Capture telemetry during a dual-card inference run (start b70tools **first**, then
launch your workload 12–15 s later):
```powershell
# Terminal 1 — 10-minute telemetry window:
.\build\b70tools.exe run --ticks 600 --out .\runs\dual-b70-1

# Terminal 2 — after ~12 s, start inference:
$env:GGML_VK_VISIBLE_DEVICES  = '0,1'
$env:GGML_VK_DISABLE_COOPMAT  = '1'
llama-cli.exe -m <model.gguf> -ngl 99 -sm layer -ts 1,1 -fit off --no-mmap -dio -c 4096 -n 2000 -p "<prompt>"
```

Analyze after:
```powershell
.\build\b70tools.exe adapters      .\runs\dual-b70-1   # ← start here; both cards' thermals tell the story
.\build\b70tools.exe summarize     .\runs\dual-b70-1
.\build\b70tools.exe disagreements .\runs\dual-b70-1
.\build\b70tools.exe self          .\runs\dual-b70-1
.\build\b70tools.exe verdict       .\runs\dual-b70-1
```

New to the hardware? The full setup guide is at
[`docs/runbook-fresh-b70-pc.md`](docs/runbook-fresh-b70-pc.md) — driver install, llama.cpp
Vulkan setup, thermal management tips, and a complete experiment walkthrough.

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
- D3DKMT kernel-mode adapter perf counters ([`src/collectors/d3dkmt_adapter_perfdata.cc`](src/collectors/d3dkmt_adapter_perfdata.cc))
- DXGI VideoMemoryInfo per-process heap budgets ([`src/collectors/dxgi_query_video_memory.cc`](src/collectors/dxgi_query_video_memory.cc))
- Vulkan VK_EXT_memory_budget ([`src/collectors/vulkan_memory_budget.cc`](src/collectors/vulkan_memory_budget.cc))
- IGCL power/thermal/clock telemetry ([`src/collectors/igcl_power_telemetry.cc`](src/collectors/igcl_power_telemetry.cc))

All samples flow through an [event bus](src/bus/event_bus.cc) to a
[delta-suppressed JSONL writer](src/schema/jsonl_writer.cc). Cross-source disagreements
are detected by [arbitration rules](src/arbitrator/disagreement_rules.cc) and tagged
in real time. GPU identity is reconciled across all four APIs via
[LUID-anchored binding](src/identity/reconciler.cc).

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

All experiments ran on 2026-05-28, in order. Each stage builds on the previous one.
Raw telemetry logs are excluded from git (large JSONL); the findings docs capture
everything material, with re-run commands so any experiment can be reproduced.

---

### Stage 1: Idle Baseline

*Characterizing the noise floor.*

**Findings:** [`docs/baseline-findings-idle.md`](docs/baseline-findings-idle.md)

Five minutes at idle, no inference running. The goal is to know what the rig looks like
when nothing is happening, so every inference delta is measured against a real reference.

```powershell
.\build\b70tools.exe run --ticks 300 --out .\runs\baseline-idle-1

.\build\b70tools.exe adapters      .\runs\baseline-idle-1
.\build\b70tools.exe disagreements .\runs\baseline-idle-1
.\build\b70tools.exe self          .\runs\baseline-idle-1
```

**What was found:** the top-slot card's IGCL telemetry is broken in three distinct
categories simultaneously — voltage (5.117 V), frequency (8.55 GHz), and activity counters
(advancing 94× faster than wall clock) — all at idle, all constant, all impossible. The
"broken card" framing was already wrong before the first inference run. The idle noise
floor settled at exactly **3 disagreement classes** that would persist unchanged across
every subsequent experiment.

---

### Stage 2: Single-GPU Inference

*Establishing the wake signal.*

**Findings:** [`docs/findings-single-gpu-mistral24b-1.md`](docs/findings-single-gpu-mistral24b-1.md)

Mistral-Small-3.2-24B Q4_K_M, bottom-slot card only. First real Vulkan inference
telemetry on this rig.

```powershell
# Terminal 1 — telemetry:
.\build\b70tools.exe run --ticks 180 --out .\runs\single-gpu-mistral24b-1

# Terminal 2 — inference on card 0 only:
$env:GGML_VK_VISIBLE_DEVICES = '0'
$env:GGML_VK_DISABLE_COOPMAT = '1'
llama-cli.exe -m Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M.gguf `
    -ngl 99 --no-mmap -dio -c 4096 -n 1500 -p "<prompt>"

# Analysis:
.\build\b70tools.exe adapters .\runs\single-gpu-mistral24b-1
```

**What was found:** the active card woke cleanly; the idle card stayed at the noise floor.
Signal-to-noise on the used card: **10× activity rate** (32% vs 3%), **5× clock** (2.8 GHz
vs 550 MHz), **+24 °C die temperature**. The "60–95% activity" expectation was wrong —
32% is the real single-card signature for 24B Q4 Vulkan inference on B70. Runbook updated.

v1 gap confirmed: b70tools cannot see the 14 GB of model weights on the GPU. DXGI VMI and
Vulkan budget are per-process; our observer process sees only its own 4 KiB allocs.

---

### Stage 3: Per-Card Baselines

*Proving both GPUs work.*

**Findings:** [`docs/findings-single-gpu-both-baselines.md`](docs/findings-single-gpu-both-baselines.md)

Same workload, run separately on each card to establish individual thermal and throughput
profiles.

```powershell
# Card 0 (Vulkan0 = bottom slot, healthy IGCL):
$env:GGML_VK_VISIBLE_DEVICES = '0'
.\build\b70tools.exe run --ticks 180 --out .\runs\single-gpu-mistral24b-1
# ... run llama-cli ...
.\build\b70tools.exe adapters .\runs\single-gpu-mistral24b-1

# Card 1 (Vulkan1 = top slot, broken IGCL):
$env:GGML_VK_VISIBLE_DEVICES = '1'
.\build\b70tools.exe run --ticks 180 --out .\runs\single-gpu-mistral24b-2
# ... run llama-cli ...
.\build\b70tools.exe adapters .\runs\single-gpu-mistral24b-2
```

**What was found:** card 0 delivered 27.3 t/s; card 1 delivered 27.8 t/s. Identical to
within noise. **The GPU hardware is fully healthy. Only the IGCL telemetry path is broken.**
"Broken card" is retired; "broken-telemetry card" is the correct framing.

The top-slot card's VRAM hit **90 °C** under solo Mistral 24B in the open-case workbench
setup — purely a thermal environment issue from the cramped slot, not GPU degradation.
Thermal sensors on the broken-telemetry card are credible; voltage/frequency/activity are
not. The per-field credibility model held.

---

### Stage 4: Concurrent Dual-Card

*New failure mode discovered.*

**Findings:** [`docs/findings-both-cards-concurrent-mistral24b-1.md`](docs/findings-both-cards-concurrent-mistral24b-1.md)

Two independent `llama-cli` processes, one per card, running simultaneously.

```powershell
# Telemetry first:
.\build\b70tools.exe run --ticks 240 --out .\runs\both-cards-concurrent-1

# Then both inference processes concurrently:
$env:GGML_VK_VISIBLE_DEVICES = '0'; $env:GGML_VK_DISABLE_COOPMAT = '1'
Start-Process llama-cli.exe -ArgumentList "-m <model> -ngl 99 --no-mmap -dio -c 4096 -n 1500 -p <prompt>" `
    -RedirectStandardOutput .\runs\both-cards-concurrent-1\llama-vk0.log -NoNewWindow

$env:GGML_VK_VISIBLE_DEVICES = '1'
Start-Process llama-cli.exe -ArgumentList "-m <model> -ngl 99 --no-mmap -dio -c 4096 -n 1500 -p <prompt>" `
    -RedirectStandardOutput .\runs\both-cards-concurrent-1\llama-vk1.log -NoNewWindow

.\build\b70tools.exe adapters .\runs\both-cards-concurrent-1
```

**What was found:** both cards delivered ~27–28 t/s (identical). But under concurrent
Vulkan initialization, `ctlPowerTelemetryGet` for the top-slot card returned non-success
for the **entire 4-minute window** — zero thermal, zero clock, zero voltage. The
disagreement count dropped from 25 to 1, not because things improved but because the
broken source went silent.

A clean disagreement report can mean healthy telemetry **or** silently failing telemetry.
This is the insight that motivated the `previously_reporting_source_went_silent` rule.
Vulkan init time also jumped from ~50 ms to 304 ms (6.1×) due to ICD serialization
contention — caught honestly by the [library audit](src/runtime/library_audit.cc).

---

### Stage 5: Dual-Card Layer Split (MoE)

*The milestone.*

**Findings:** [`docs/findings-dual-b70-qwen30b-moe-1.md`](docs/findings-dual-b70-qwen30b-moe-1.md)

Qwen3-30B-A3B (MoE, 3B active params/token, ~17 GB Q4_K_M), layer-split across both
cards. The experiment this whole project was built for.

```powershell
# Telemetry first — always start b70tools before the workload:
.\build\b70tools.exe run --ticks 600 --out .\runs\dual-b70-qwen30b-moe-1

# Wait 12-15 s, then inference:
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'
llama-cli.exe -m Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf `
    -ngl 99 -sm layer -ts 1,1 -fit off --no-mmap -dio -c 4096 -n 2000 -p "<prompt>"

.\build\b70tools.exe adapters      .\runs\dual-b70-qwen30b-moe-1
.\build\b70tools.exe disagreements .\runs\dual-b70-qwen30b-moe-1
```

**What was found:** both cards hit **identical 61 °C die peaks** — the clearest evidence
the layer split engaged both equally. Generation throughput: **81.7 t/s** (3× better than
single-card 27 t/s). The `-fit off` flag is critical; without it, llama.cpp auto-routes
all layers to one card. The silence cascade that had caused prior system lockups did not
trigger. The IGCL silence rule fired zero false positives across ~25 minutes of cumulative
operation.

State B airflow improvement (pre-run) dropped the cramped top-slot card's VRAM from
90 °C (State A solo) to **64 °C** — a 26 °C reduction from the combination of better
airflow and halved per-card load.

---

### Stage 6: Dual-Card Layer Split (Dense)

*MoE vs dense comparison.*

**Findings:** [`docs/findings-dual-b70-qwen25-32b-q4-1.md`](docs/findings-dual-b70-qwen25-32b-q4-1.md)

Qwen2.5-32B-Instruct Q4_K_M (~18.5 GB dense), same dual-split setup. Dense model
comparison to the MoE experiment above.

```powershell
.\build\b70tools.exe run --ticks 600 --out .\runs\dual-b70-qwen25-32b-q4-1

$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'
llama-cli.exe -m qwen2.5-32b-instruct-q4_K_M.gguf `
    -ngl 99 -sm layer -ts 1,1 -fit off --no-mmap -dio -c 4096 -n 2000 -p "<prompt>"

.\build\b70tools.exe adapters .\runs\dual-b70-qwen25-32b-q4-1
```

**What was found:**

| | Qwen3-30B MoE | Qwen2.5-32B dense |
|---|---|---|
| Prompt eval | 30.1 t/s | **242.2 t/s** (8× faster) |
| Generation | **81.7 t/s** | 20.7 t/s (4× slower) |
| Die temp peak | 61 °C | 66 °C |
| VRAM temp peak (top card) | 64 °C | 74 °C |
| Fan peak | 1083 RPM | 1370 RPM |

**MoE for fast iterative work; dense for structured output or deep analysis.** The
"+5–10 °C across the board" thermal delta on dense is the telemetry signature of
sustained full-parameter compute vs sparse-activation inference.

---

## Performance Reference

Measured on this rig. Driver 32.0.101.8801. Open workbench (State B airflow).

```
Workload                          Config        Gen t/s   Prompt t/s   VRAM/card
Mistral-Small-3.2-24B Q4_K_M     Single card    27.3      ~400–443      14 GB
Qwen3-30B-A3B MoE Q4_K_M         Dual split     81.7        30.1        8.5 GB
Qwen2.5-32B-Instruct Q4_K_M      Dual split     20.7       242.2        9.3 GB
```

Observation cost (all runs): **16.6 MiB RSS · <130 ms init · 0 watchdog kicks · PASS**

Full run index with raw log sizes, timing charts, and thermal tables:
[`b70tools_5_28_repo_status.md`](b70tools_5_28_repo_status.md)

---

## Known Rig-Specific Issues

Stable, characterized, filed in the noise floor. Do not affect inference quality —
only what b70tools can credibly report about it.

| Issue | Card | Impact |
|---|---|---|
| IGCL reads wrong registers (5.117 V / 8.55 GHz at idle) | Top slot | Voltage and freq unusable; temperature is credible |
| IGCL activity counters run ~45–94× wall clock | Top slot | Activity counters unusable for this card |
| IGCL goes completely silent under concurrent Vulkan init | Top slot | Zero telemetry during contention; silence rule fires if it recurs |
| D3DKMT `ADAPTERPERFDATA` returns INVALID_PARAMETER | Both | Expected on Win10 19045; flagged once per session |
| Workload VRAM residency invisible | Both | DXGI/Vulkan budget are per-process; model weights are invisible to v1 |

The VRAM blindspot is the most operationally significant gap. v1.5's PDH `GPU Process
Memory` collector is the fix.

---

## Status and Whats Next

**v1 is complete.** All planned experiments have been run. The noise floor is
characterized, the do-no-harm budget held across every run, and the architecture is stable.

**v1.5 priorities:**
1. PDH `GPU Process Memory` — removes the VRAM blindspot
2. `Idle → Awake → ActiveCompute` FSM transitions
3. Generalized `previously_reporting_source_went_silent` rule
4. Replay fixtures for regression testing

---

## Repo Layout

```
src/
  arbitrator/      disagreement_rules.cc — cross-source conflict detection
  bus/             event_bus.cc — metric pub/sub
  collectors/      d3dkmt, dxgi, vulkan, igcl, pdh, host_memory
  identity/        reconciler.cc — LUID-anchored cross-API binding
  runtime/         poll_loop, session, watchdog, library_audit, driver_fingerprint
  schema/          events, metric_sample, jsonl_writer, delta_filter
  tools/           adapters, summarize, disagreements, self, verdict (CLI verbs)
  main.cc
docs/              findings docs + runbooks
eval/              automated model evaluation framework + scripts
third_party/       Vulkan-Headers (Khronos), IGCL (Intel)
build.ps1          build script (locates MSVC, runs CMake + Ninja)
CMakeLists.txt
b70tools_5_28_repo_status.md   full run index, timing charts, status snapshot
```

Raw telemetry logs (`runs/`) are excluded from git — large JSONL, regenerable. The
findings docs capture everything material from each run, with re-run commands.
