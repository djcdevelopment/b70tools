# baseline-idle-1 — findings

**Run:** `D:\work\b70tools\runs\baseline-idle-1\events.jsonl`
**When:** 2026-05-28, ~03:55–04:00 PDT.
**Duration:** 304.1 s (300 ticks at 1 Hz + jitter + startup).
**Workload:** none. Normal desktop allowed (Claude Code session, Windows shell).
**JSONL size:** 323.7 KiB / 1376 lines / 240.9 bytes per event.
**Collectors:** all four green — D3DKMT (TrulyPassive), DXGI VMI (TrulyPassive), Vulkan budget (DriverPassive), IGCL (DriverPassive).
**AdapterState reached:** both adapters → `Idle` (FSM does not yet advance further; see Finding 6).

This is the noise-floor reference for the upcoming single-GPU and dual-B70 inference runs. Anything new the inference runs surface will be measured against this.

---

## Headline result

`b70tools` runs stably for 5 minutes at 1 Hz, produces 323 KiB of structured JSONL, and **`summarize` correctly classifies the rig's noise floor down to 3 unique disagreement classes**. The architecture is operating as designed; the deltas during inference are now interpretable.

## Disagreement noise floor (3 unique classes across 304 seconds)

| Rule | Adapter | Count | Span | Notes |
|---|---|---|---|---|
| `expected_source_unavailable` | adapter_00011b4f | 1 | n/a (once per session) | D3DKMT `KMTQAITYPE_ADAPTERPERFDATA` returns `STATUS_INVALID_PARAMETER`. Known from M2. |
| `physically_impossible_voltage` | adapter_00012fbe | 12 | 282.5 s | IGCL reports `gpu.voltage_v = 5.117 V`. Bogus. New v1.5 rule firing as designed. |
| `physically_impossible_frequency` | adapter_00012fbe | 12 | 282.5 s | IGCL reports `gpu.frequency_hz = 8.550 GHz`. Bogus. New v1.5 rule firing as designed. |

**Important architectural confirmation:** at 1 Hz × 300 ticks we expected up to 600 firings per impossibility rule (if the rule fires on every poll). We saw 12. The **delta-suppression filter** at the metric layer naturally rate-limits these — the impossible values are constant, so they're emitted at the 30 s heartbeat cadence rather than every tick. **No rule-level dedup needed.** Roughly one impossibility report per 25 s wall time, which matches the 30 s heartbeat plus jitter.

## Real telemetry behavior at idle

### Activity counters at "idle" (corrected via `adapters` verb)

`adapter_00011b4f` (the healthy card) at idle, **computed within-run via `b70tools adapters`** (Δcounter / Δwall):
- `gpu.activity.global_counter` Δ over 303.9 s: **+19.5 s → 6.4% activity**
- `gpu.activity.render_compute_counter` Δ: **+9.0 s → 3.0% activity**

(An earlier draft of this doc claimed ~31% based on comparing peak deltas *across runs*, which conflates baseline + setup time — the within-run rate is the correct idle metric, and it's much lower.)

3–6% is plausible for DWM + browser GPU acceleration + the Claude desktop session at idle. Implications for inference experiments:

- Use **within-run rate** (visible in `b70tools adapters <run>`) to identify inference-attributable wake.
- A clean single-GPU inference experiment should show this rate move **well above ~6% on the used card** — at the millisecond-budget end you'd expect 60–95% activity sustained during token generation.
- A clean dual-GPU experiment should show both cards' activity rise materially above the baseline floor.

### `adapter_00012fbe`'s broken IGCL extends beyond voltage/frequency

**This is the new finding the baseline surfaced.** During the M2 run, only `gpu.voltage_v` and `gpu.frequency_hz` looked obviously bogus on adapter_00012fbe. The baseline shows the activity counters are also degraded:

| Metric | adapter_00011b4f (healthy) | adapter_00012fbe (degraded) | Verdict |
|---|---|---|---|
| `gpu.activity.global_counter` Δ over 304 s | +109 s | +28,617 s | **94× faster than wall clock — structurally impossible** |
| `gpu.activity.render_compute_counter` Δ | +94 s | +28,633 s | **same; activity counters are degraded too** |
| `gpu.activity.media_counter` Δ | +0.3 s | +1,838 s | same pattern |
| `card.energy_j_counter` Δ | +0 (counter stuck at 0) | +2.1M J | first card's counter is dead, second card is high but moves |

`adapter_00012fbe`'s IGCL is broken in **three distinct categories** simultaneously:
1. Voltage register reports a 5V rail value.
2. Frequency register reports 8.55 GHz.
3. Activity / energy counters tick ~94× faster than wall time.

That's not random — it's structurally consistent. **Working hypothesis:** IGCL on this driver/card combination is reading the wrong device's registers (the wrong physical adapter index, or a shared/shifted MMIO base). All three pathologies are explained by "wrong source register, but real."

**`card.energy_j_counter` stuck at 0 on adapter_00011b4f** is a separate, smaller anomaly worth noting — the healthy card's energy counter doesn't increment. We see frequency/voltage/temp/activity on it, so it's not a wholesale telemetry failure; just this one field.

### Clock / thermal stability (healthy card)

| Metric | M2 snapshot | Baseline peak | Drift |
|---|---|---|---|
| `gpu.frequency_hz` (adapter_00011b4f) | 400 MHz | 550 MHz | mild boost under background load |
| `gpu.voltage_v` | 0.770 V | 0.775 V | flat |
| `gpu.temperature_c` | 48 °C | 49 °C | +1 °C over 5 min, equilibrium reached |
| `vram.frequency_hz` | 2.375 GHz | 2.375 GHz | constant |

Reasonable. The healthy card's thermal and frequency telemetry are credible.

### Memory residency at idle

Both adapters: `vram.local.current_usage_bytes = 4 KiB` peak, `vulkan.heap0.usage_bytes = 4 KiB` peak. **`b70tools` itself never allocates GPU memory** — those 4 KiB are loader/ICD scratch. DXGI VMI is reporting per-process; our process has essentially zero residency. Workloads' allocations will show up under their own PIDs (which v1 doesn't attribute — v1.5 with `PDH GPU Process Memory full set`).

### `vram.local.budget_bytes = 31.12 GiB` on both cards

Same as M2. The driver reservation is consistent. Healthy.

## Collector stability

| Collector | Init wall | RSS delta | Modules added | Stable? |
|---|---|---|---|---|
| `d3dkmt_adapter_perfdata` | 26.9 ms | +8 KiB | 0 | yes |
| `dxgi_query_video_memory` | 23.4 ms | +4 KiB | 0 | yes |
| `vulkan_memory_budget` | 51.5 ms | +16.6 MiB | 15 | yes (DriverPassive; expected) |
| `igcl_power_telemetry` | 25.0 ms | +8 KiB | 0 | yes (IGCL DLLs already in process via Vulkan) |

No watchdog kicks. No collector ran slow. Init costs were comparable to M2 (within a few ms each).

## Observation cost stability

- Total `b70tools` RSS attributable to collector inits: **+16.6 MiB**. No growth observed over 304 s.
- JSONL write rate: ~1.1 KiB / s (323 KiB / 304 s). Sustainable indefinitely on any SSD.
- Bytes per emitted event: **240.9 B**. Reasonable for our compact-keyed JSONL.
- No SIGINT was needed; the run terminated at `--ticks 300`.

We are **comfortably inside the do-no-harm budget** (target < 50 MB RSS, achieved ~17 MB attributable + the executable itself).

## Findings that change the runbook

### Finding 1 — `b70tools` will NOT show `ActiveCompute` during inference under the current FSM

The `AdapterStateFsm` only handles `Unknown → Idle` transitions in v1. Even with the IGCL activity counter rising on adapter_00011b4f at idle, the FSM stayed at `Idle`. **Implication:** during the dual-B70 inference experiment, operators must look at IGCL activity-counter **deltas** (not the FSM `state reached`) to determine whether the workload exercised both cards.

**Action:** runbook updated to call this out (see operational-runbook.md §Experiment 3 "open questions"). v1.5 adds `Idle → Awake → ActiveCompute` transitions driven by activity-counter delta thresholds.

### Finding 2 — Idle baseline activity is NOT zero on this rig (corrected: ~3–6%, not 31%)

Within-run activity rates at idle: **~6.4%** `global_counter`, **~3.0%** `render_compute_counter`. Attributable to DWM + browser + Claude desktop session. Inference runs must be measured as **deltas from this baseline**, not against zero. The new `b70tools adapters <run>` verb exposes the rate cleanly per adapter.

### Finding 3 — adapter_00012fbe's IGCL telemetry is degraded across at least 4 fields

Voltage, frequency, activity counters, and energy counter all show patterns consistent with "IGCL is reading the wrong device's registers." For the inference runs, **ignore adapter_00012fbe's IGCL telemetry and use DXGI VMI + Vulkan budget as primary signals for that card.** This is exactly the fallback hierarchy plan §A.10 was written for.

### Finding 4 — `b70tools self` (the v1.5 verb) would catch RSS drift if it happened

Right now we have no per-tick observation-cost metric (the audit records are one-shot at init). For longer inference runs, a `self` metric emitted each tick would let us verify RSS stability over time. **Action:** when the inference runs land, if observation cost looks suspicious, the next opportunistic verb is `b70tools self <run>` summarizing audit records + (added) per-tick RSS metrics.

## Hand-off to inference experiments

Baseline → ready. The four collectors are stable; the noise floor is characterized; the impossibility rules tag the known bogus telemetry on adapter_00012fbe without drowning the signal.

Next: **Experiment 2 — single-GPU inference run**, per `docs/operational-runbook.md`. Operator runs the Vulkan inference workload on one B70; `b70tools run --ticks 0` captures the session; `summarize` + `disagreements` afterwards.

The key questions Experiment 2 should answer:

1. Does `gpu.activity.render_compute_counter` on adapter_00011b4f visibly accelerate above the ~31% baseline rate?
2. Does `vram.local.current_usage_bytes` rise to match the workload's model size?
3. Are there any *new* disagreement classes beyond the 3 listed above?
4. Does observation cost stay flat?

If all four answer cleanly, **Experiment 3 — dual-B70 split** is the next step.
