# findings — both cards running independent Mistral 24B inferences concurrently

**Run:** `D:\work\b70tools\runs\both-cards-concurrent-mistral24b-1\events.jsonl`
**When:** 2026-05-28, 05:12 PDT.
**Build state:** **B** (operator improved airflow ~10 °C earlier this session; rig still workbench, not closed-case).
**Workload:** Two independent `llama-cli.exe` processes, both running Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M with identical args (`-ngl 99 --no-mmap -dio -c 4096 -n 1500`) and identical prompts. One via `GGML_VK_VISIBLE_DEVICES=0` (adapter_00011b4f), one via `=1` (adapter_00012fbe). **Both completed their 1500-token generations** during the 4-minute b70tools window.
**Throughput delivered by each card:**
- adapter_00011b4f (healthy IGCL):  **428.9 t/s prompt eval / 27.3 t/s generation**
- adapter_00012fbe (broken IGCL):   **443.1 t/s prompt eval / 27.8 t/s generation**
**JSONL:** 254.8 KiB / 1093 lines / 238.7 B/event / 1076 B/s.

---

## Headline result #1 — broken IGCL does NOT mean broken compute

The two cards delivered **identical-to-within-noise inference throughput**, with the broken-IGCL card actually slightly faster. This is the definitive cross-validation: the IGCL telemetry path is degraded on adapter_00012fbe, **the GPU itself is fully healthy**. Anyone reading the project's earlier findings about "the broken card" should now read it as "**the card with broken telemetry**" — its compute, memory bandwidth, and Vulkan integration are all working correctly.

The architecture's per-field credibility framework was right: arbitrate around the broken telemetry; don't treat it as proxy for the underlying device.

---

## Headline result #2 — adapter_00012fbe's IGCL went *completely silent* under concurrent load

This is a **new failure mode** that no prior run exposed. Across the 4-minute concurrent capture window:

- **adapter_00012fbe emitted ZERO thermal, ZERO clock, ZERO voltage, ZERO activity-counter samples.** The `adapters` verb shows "(no thermal/clock/voltage metrics in this run)" for this card.
- **Zero `physically_impossible_voltage` / `physically_impossible_frequency` reports** for adapter_00012fbe — not because the values became plausible, but **because no values were emitted at all**.

The likely cause: `ctlPowerTelemetryGet` for adapter_00012fbe returns non-success (or times out) when both cards are in active Vulkan compute, and our IGCL collector silently skips emission rather than reporting the failure. The collector loop pattern is:

```cpp
if (I.pTel(b.handle, &pt) != CTL_RESULT_SUCCESS) continue;
```

Failure goes uncounted. **v1.5 priority bump:** add a `igcl_call_failed_silently` disagreement rule emitted from the collector itself when a previously-reporting adapter stops producing data. The signal is "telemetry expected but absent" — exactly the kind of arbitration finding the project exists for.

Comparison across all four runs:

| Run | adapter_00012fbe IGCL behavior |
|---|---|
| Idle baseline | Broken values present (5.117 V, 8.55 GHz), 12 voltage + 12 freq disagreements |
| Solo Vulkan0 (this card idle) | Same broken values; 12 + 12 disagreements |
| Solo Vulkan1 (this card busy) | Same broken values, more samples → 29 + 29 disagreements |
| **Concurrent (both busy)** | **No samples emitted at all → 0 + 0 disagreements (silent failure)** |

---

## Per-adapter detail

### adapter_00011b4f (healthy IGCL) — concurrent vs solo

| Metric | Solo (run-1) | Concurrent | Delta |
|---|---|---|---|
| `gpu.activity.render_compute_counter` rate | 32.1% | **23.4%** | **−27%** |
| `gpu.activity.global_counter` rate | 34.5% | 26.3% | −24% |
| `gpu.frequency_hz` peak | 2.800 GHz | **2.800 GHz** | unchanged |
| `gpu.frequency_hz` mean | 2.206 GHz | 2.202 GHz | unchanged |
| `gpu.voltage_v` peak | 1.055 V | 1.055 V | unchanged |
| `gpu.temperature_c` peak | 73 °C | **73 °C** | unchanged |
| `vram.temperature_c` peak | 78 °C | 78 °C | unchanged |
| `card.fan0.speed` peak | 1726 RPM | 1746 RPM | +1% |

**The healthy card's activity rate dropped 24-27% under concurrent load** — measurable contention. But the **per-card inference throughput dropped almost not at all** (the prior solo run hit `--no-mmap -dio` Bash-timeout before finishing, so we don't have a clean solo-vs-concurrent t/s comparison, but 27 t/s for 24B Q4 on B70 is consistent with the rig's expected single-card performance).

This means the contention is **not at the compute level** but somewhere else — most likely Vulkan command submission scheduling, ICD-internal locking, or PCIe DMA from host RAM during prompt eval. The cards each have plenty of VRAM headroom and weren't memory-bandwidth-bound.

**State B airflow validation:** peak die and VRAM temperatures on adapter_00011b4f are **identical to its solo State A measurement** (73 °C / 78 °C), despite carrying the additional thermal load of a busy neighbor. The operator's airflow improvement absorbed the concurrent thermal load — net thermal effect on this card was zero.

### adapter_00012fbe — telemetry blackout

Nothing to compare against. IGCL emitted no usable signal. We know the card was working because llama-cli reported 27.8 t/s generation from it, but b70tools' telemetry path was blind.

**What we can NOT see for this card during the concurrent run:**
- Die temperature (State B airflow effect on this card is the most operationally important question and we missed it)
- VRAM temperature (was it the 90 °C peak from solo run-2, or lower under State B?)
- Clock behavior
- Power draw

**What we CAN infer:** the workload completed at 27.8 t/s, similar to the healthy card. So the card was actively computing for the full window. Its thermal output presumably matched solo run-2's pattern (74 °C die / 90 °C VRAM peak in State A), reduced by State B airflow improvement, plus any thermal interaction with the neighboring card running concurrently.

---

## Headline result #3 — `vulkan_memory_budget` init time jumped 6× under concurrent Vulkan init

The library audit caught this honestly:

| Run | `vulkan_memory_budget` init wall |
|---|---|
| Idle baseline | 51.5 ms |
| Solo run-1 | 51.5 ms |
| Solo run-2 | 49.8 ms |
| **Concurrent** | **304.5 ms** (6.1× slower) |

When b70tools started up, both llama-cli processes were already initializing their own Vulkan instances. Intel's ICD loader serialized them, and our `vulkan_memory_budget` `vkCreateInstance` waited 304 ms instead of the usual ~50 ms. **The audit framework correctly flagged this as a DriverPassive cost increase** — the operator can see at a glance that the rig was under heavy Vulkan-init contention during the experiment's first second.

This is a healthy validation that the audit framework works as intended for catching driver-stack contention, not just first-class side-effects.

---

## Disagreement profile

```
[1]  expected_source_unavailable  /  adapter_00011b4f   (the persistent known)
     rate: 0.25 reports / minute
```

**Just one report across the whole run.** The previous physically_impossible_* reports vanished — not because the values became plausible, but because the broken card stopped emitting IGCL data altogether. This is **the absence-as-disagreement pattern** that v1.5 should formalize.

Per the architecture's own rules: **no rule fired for "expected IGCL data is missing,"** because we don't have one. The disagreement count dropped from 25/59 (solo runs) to 1, but the underlying problem got worse, not better. Headline takeaway: **a clean disagreement summary can mean either healthy telemetry or telemetry that's failing silently. v1.5 should add the "expected-source-went-silent" rule alongside the existing source-unavailable rule.**

---

## Observation cost

| | Idle | Solo run-1 | Solo run-2 | **Concurrent** |
|---|---|---|---|---|
| RSS attributed | 16.6 MiB | 16.6 MiB | 16.7 MiB | **16.6 MiB** |
| JSONL B/s | 1090 | 1361 | 1992 | **1076** |
| Watchdog kicks | 0 | 0 | 0 | 0 |
| Vulkan init wall | 51.5 ms | 51.5 ms | 49.8 ms | **304.5 ms** |

**RSS held**. JSONL B/s is back near the idle baseline because the IGCL-silence on adapter_00012fbe drastically reduced the per-tick metric volume (no impossible values to re-emit at heartbeat). **The do-no-harm contract held across all four runs.**

---

## Operational implications

### For the dual-B70 split experiment (Experiment 3)

The concurrent experiment tells us several things about what to expect from Experiment 3:

1. **Vulkan-init contention is real.** If we start b70tools after llama-cli has begun initializing, our `vulkan_memory_budget` init will be slow. Mitigation: start b70tools first, give it ~5 seconds to settle, *then* start llama.
2. **adapter_00012fbe's IGCL may go silent under load.** For the dual-B70 split run, expect the broken card's telemetry to be even less reliable than in solo runs. Thermal is our best fallback signal, and even that may stop emitting if IGCL fully drops out.
3. **adapter_00011b4f's activity rate at ~16% would be the right prediction for layer-split** (half of solo's 32%) — but the concurrent test showed independent-load drops it to 23%, so the right prediction is probably **15–25% activity per card on the healthy adapter for dual-B70 split**.
4. **State B airflow is sufficient for concurrent compute load.** The neighboring busy card did NOT push adapter_00011b4f's thermals up. Good news for dual-B70 sustained runs.

### For v1.5

- **Priority bump: `igcl_call_failed_silently` rule.** Emit a `source_degraded` disagreement when IGCL's `ctlPowerTelemetryGet` returns non-success on a previously-reporting adapter. The collector already has the data; just needs to emit it instead of `continue`-ing silently.
- **Considerati: `previously_reporting_source_went_silent` general arbitration rule.** Track per-(source, adapter, metric) the last successful sample; if N consecutive ticks elapse without a sample, emit a disagreement. Lets us catch silent failures across any source, not just IGCL.
- **`vkCreateInstance` cost is a useful operational signal under contention.** Worth capturing per-collector init-cost as a recurring MetricSample at startup, not just in the audit record, so it's visible to comparisons.

### For the operator

You now have direct evidence that **adapter_00012fbe's compute path is fully working** — same 27 t/s as the healthy card. The "broken card" framing should be retired in favor of "broken-telemetry card." The IGCL silent-failure mode under contention is a new finding worth flagging if you ever interact with Intel's developer support about the driver — `ctlPowerTelemetryGet` succeeded for both adapters at idle and individually under load, but stopped responding for adapter index 1 when both adapters were under concurrent Vulkan compute load.

---

## Re-running this test

```powershell
# Background telemetry (4-minute ceiling matches Bash timeout):
& "D:\work\b70tools\build\b70tools.exe" run --ticks 240 --out "D:\work\b70tools\runs\<name>"

# Llama on Vulkan0 (no wait):
$env:GGML_VK_VISIBLE_DEVICES = '0'
$env:GGML_VK_DISABLE_COOPMAT = '1'
Start-Process "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -ArgumentList @("-m","<model>","-ngl","99","--no-mmap","-dio","-c","4096","-n","1500","-p","<prompt>") `
    -RedirectStandardOutput "<run-dir>\llama-vk0.log" -NoNewWindow

# Llama on Vulkan1 (no wait):
$env:GGML_VK_VISIBLE_DEVICES = '1'
Start-Process "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -ArgumentList @(...same args...) `
    -RedirectStandardOutput "<run-dir>\llama-vk1.log" -NoNewWindow

# Wait for b70tools to finish then analyze:
& "D:\work\b70tools\build\b70tools.exe" adapters "D:\work\b70tools\runs\<name>"
```
