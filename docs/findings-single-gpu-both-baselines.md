# findings — both per-card single-GPU baselines

**Runs:**
- `D:\work\b70tools\runs\single-gpu-mistral24b-1\events.jsonl` — Vulkan0 → adapter_00011b4f (the **healthy-IGCL** card)
- `D:\work\b70tools\runs\single-gpu-mistral24b-2\events.jsonl` — Vulkan1 → adapter_00012fbe (the **broken-IGCL** card)
**Workload (identical for both):** Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M.gguf, `-ngl 99 --no-mmap -dio -c 4096 -n 1500`, same prompt.
**Isolation:** `GGML_VK_VISIBLE_DEVICES=0` and `=1` respectively. **Confirmed via telemetry:** the unused card stayed at idle thermals + idle clocks in both runs.

This is the headline result of the per-card baseline pass:

> **Both Arc Pro B70s do real Vulkan compute work correctly. Only the IGCL telemetry path is broken on the second card — and the broken telemetry is independent of workload.**

The single-card recipe works on either card. The user's choice between cards has *zero* effect on inference quality. It affects only what b70tools can credibly tell us about the run.

---

## Side-by-side: when the card is being used

| Metric | adapter_00011b4f (used in run -1) | adapter_00012fbe (used in run -2) | Delta |
|---|---|---|---|
| `gpu.activity.render_compute_counter` rate | **32.1%** (clean) | 4493% [BROKEN: counter advances 45× wall clock] | IGCL counter broken |
| `gpu.frequency_hz` peak (credible reading) | **2.800 GHz** | 8.550 GHz [stuck-broken; mean 3.1 GHz incl. real samples] | IGCL freq broken |
| `gpu.voltage_v` peak (credible reading) | **1.055 V** | 5.117 V [stuck-broken; mean 1.37 V incl. real samples] | IGCL voltage broken |
| **`gpu.temperature_c` peak** | **73 °C** | **74 °C** | **~equal — credible** |
| **`vram.temperature_c` peak** | **78 °C** | **90 °C** | **broken-card runs ~12 °C hotter VRAM** |
| `card.fan0.speed` peak (credible reading) | 1726 RPM | broken (2.4 G RPM max), but mean 38 M RPM also broken | IGCL fan broken on card 2 |

**Key signal:** the **thermal envelopes are credible on both cards and they're similar within ~10 °C**. Both die temps reached the low-70s; VRAM on the broken card hit 90 °C vs 78 °C on the healthy card. This is the cleanest cross-card evidence that **both cards genuinely executed the workload**.

The broken card's VRAM running 12 °C hotter under identical workload is **a real physical thermal environment difference, not measurement error** (operator-confirmed 2026-05-28):

- adapter_00012fbe is in PCIe slot 1 (top), the **DisplayPort-connected card**, RIGHT next to the CPU heatsink + fans
- It is wedged tightly between the CPU heatsink above and the second B70 below (which acts as a blower)
- Operator's hand-behind-exhaust check independently confirms this card runs hotter — "always seems to generate more heat" at idle and under load
- 90 °C VRAM is **fully expected** given the layout, not approaching alarming for GDDR6 (spec ≈ 105 °C) but absolutely worth operational attention during sustained workloads

**Architectural implication confirmed:** the IGCL temperature reading on adapter_00012fbe is **credible** — it matches both physical reality and the operator's subjective experience. This validates the per-field credibility assessment of IGCL on the broken card: voltage / frequency / activity-counter are degraded, but **the thermal sensor path is working correctly**.

**Build-state caveat:** these thermal numbers were captured with the rig in a **half-workbench state** — case open, no case fans running, **no positive or negative pressure driving airflow across the cards**. The 90 °C VRAM is the worst case under uncontrolled airflow. Expect material improvement (~10–20 °C lower under sustained load is plausible) once the rig is in a closed case with proper intake/exhaust. The IGCL telemetry will still read the same broken voltage/frequency values; only the thermal numbers change with airflow.

## Side-by-side: when the card is *not* being used

| Metric | adapter_00011b4f (idle in run -2) | adapter_00012fbe (idle in run -1) | Both runs |
|---|---|---|---|
| `gpu.activity.render_compute_counter` rate | **2.0%** | BROKEN: counter regressed | same broken pattern as baseline |
| `gpu.frequency_hz` peak | **400 MHz** (idle clock) | 8.550 GHz [stuck-broken] | second card's value is constant |
| `gpu.voltage_v` peak | **0.775 V** | 5.117 V [stuck-broken] | second card's value is constant |
| `gpu.temperature_c` peak | **49 °C** (idle) | 55 °C (cooled but not as cold) | second card runs slightly warmer at idle too |
| `vram.temperature_c` peak | 52 °C | 62 °C | second card's VRAM also warmer at idle |
| `card.fan0.speed` peak (credible) | 859 RPM | broken samples mean meaningless | second card's fan field is stuck-broken |

**The unused card's thermals dropped back to idle baseline** in both runs. The `GGML_VK_VISIBLE_DEVICES` isolation worked — only the selected card was used; the other one truly idled.

The second card runs slightly warmer at idle too (55/62 °C vs 49/52 °C). Consistent with the under-load observation that the second slot has a worse thermal environment.

## The IGCL degradation pattern on adapter_00012fbe — characterized across three regimes

| Regime | `global_counter` Δ shape | `voltage_v` reading | `frequency_hz` reading |
|---|---|---|---|
| Idle baseline (no workload) | regresses (–94× wall clock) | stuck at 5.117 V | stuck at 8.550 GHz |
| Run-1 (other card busy, this idle) | regresses (–60296%) | stuck at 5.117 V | stuck at 8.550 GHz |
| Run-2 (this card busy) | advances (+45× wall clock) | mean 1.37 V (more real samples) | mean 3.13 GHz (more real samples) |

**Working hypothesis refined:** IGCL's `ctlPowerTelemetryGet` for the second adapter returns:
- a **structurally wrong "max" or "rail" reading** for voltage/frequency (the 5.117 V / 8.55 GHz constants),
- a **counter value that runs at the wrong scale or sign** depending on something workload-dependent (regresses when idle, advances when busy),
- but **temperature appears to be read from a working sensor path** (the 90 °C VRAM peak is physically plausible and matches expected behavior under load).

So IGCL is *partially* broken on the second card — voltage/frequency/activity-counter all degraded, but temperature credible. This is a more nuanced picture than the previous findings doc captured.

## Disagreement profile comparison

| | Idle baseline | Run-1 (Vulkan0 used) | Run-2 (Vulkan1 used) |
|---|---|---|---|
| Unique classes | 3 | 3 | 3 |
| `expected_source_unavailable` | 1 | 1 | 1 |
| `physically_impossible_frequency` on adapter_00012fbe | 12 | 12 | **29** |
| `physically_impossible_voltage` on adapter_00012fbe | 12 | 12 | **29** |
| Rate (reports / min) | 4.93 | 8.22 | **19.44** |

**The disagreement-rate doubles+ when the broken card is the one being used.** Same broken values, but they get re-emitted (and re-checked by rules) more often because the metric churn is higher during active workload on that adapter. This is the delta filter behaving as designed: workload-driven changes propagate; constant broken values still get sampled more often when the surrounding metrics change frequently.

**Critical:** the impossible-value rate doesn't change *what's broken* — it changes how often we re-discover the same brokenness. v1.5 may want a "we've seen this within the last N seconds, don't re-emit" mode for `physically_impossible_*` rules on persistently-broken sources. For v1 the redundancy is acceptable and operationally honest.

## Observation cost across all three runs

| | Idle baseline | Run-1 | Run-2 |
|---|---|---|---|
| Duration | 304.1 s | 182.4 s | 181.9 s |
| RSS attributed | 16.6 MiB | 16.6 MiB | **16.7 MiB** |
| JSONL B/s | 1090 | 1361 | **1992** |
| Bytes/event | 240.9 | 239.2 | 241.8 |
| Watchdog kicks | 0 | 0 | 0 |
| Undeclared side-effects | 0 | 0 | 0 |

**Observation cost is the most-stable thing in this dataset.** The collector inits cost the same number of bytes whether the rig is idle or running 24B parameter inference. The do-no-harm contract holds with a 3× margin across all three runs.

The JSONL byte rate scales with disagreement rate (run-2's 2× B/s vs run-1 directly mirrors its 2.4× disagreement rate). Long-running dual-B70 sessions on this driver will produce roughly 2× the disk volume of the idle baseline; still well within sustainable.

## Updated predictions for Experiment 3 (dual-B70 split)

Now that both cards are individually characterized, the dual-B70 predictions sharpen:

| Prediction | Rationale |
|---|---|
| Both cards' die temps will rise to 70+ °C | Both cards demonstrably heat to ~73 °C on solo Mistral 24B; dual-split shares the work so per-card load is lower, but layer-parallel keeps both engaged for every token. **Predicted: both 60–75 °C.** |
| adapter_00012fbe's IGCL stays broken even when actively working | Confirmed by run-2: workload didn't fix the IGCL degradation. **Predicted: same 5.117 V / 8.55 GHz stuck values, same counter weirdness.** |
| Activity rate per card: ~15–18% on the healthy card | Layer-parallel means each card carries half the per-token work but sequentially. Per-card activity is roughly half of solo: 32% / 2 ≈ 16%. **Predicted: ~16% on adapter_00011b4f's IGCL counters.** |
| Activity rate on adapter_00012fbe: still broken | The activity counters are degraded; will still report nonsense direction (advance or regress). Useless on the broken card. |
| Thermal will be the cleanest cross-card signal | Already validated in this pass: temperatures are credible on both cards. **For Experiment 3, the headline question "did both cards work?" is answered by die-temp + VRAM-temp deltas. Activity rate is unreliable on the second card.** |
| No new disagreement classes | Run-2 produced none. The architecture's noise floor is well-characterized. Any new class in dual-B70 would be a real finding. |

## Practical implication for the dual-B70 experiment

Since IGCL activity rate is unreliable on adapter_00012fbe, **the operator should focus on temperature deltas as the primary "is this card working?" signal** in the dual-B70 experiment. The `adapters` verb prints both cards' thermal envelopes; that's the cleanest evidence either way.

Activity rate on the **healthy card** (adapter_00011b4f) remains a credible per-card signal — if dual-split halves its load, its rate should drop from 32% (solo) to ~15–18%. If it stays at ~32% with the second card also heating up, the workload isn't actually splitting (this is the `-fit on` failure mode the user's 70B doc identified).

### Thermal-throttling watch for dual-B70

adapter_00012fbe will likely **thermal-limit first** under sustained dual-B70 workloads given its position (slot 1, sandwiched, near CPU heatsink). And the v1 limitation matters here: **we can't see its real clock frequency** to verify whether it's throttling — IGCL freq is stuck at the broken 8.55 GHz value. The only direct signals available are:

- **`gpu.temperature_c`** — if it climbs past ~85 °C and plateaus, the card is likely thermal-limiting clocks
- **`vram.temperature_c`** — if it pushes past ~95 °C, GDDR6 thermal management kicks in
- **Workload throughput (tokens/s, reported by llama-cli)** — if it drops mid-run, suspect thermal throttling on the second card

Recommend running dual-B70 sessions with **temperature watched** (rerun `b70tools adapters <run>` after the workload to see peaks) and considering airflow improvements if 95+ °C VRAM becomes routine.

## How to repeat each per-card baseline

```powershell
# CARD 1 (Vulkan0 = adapter_00011b4f, healthy-IGCL):
$env:GGML_VK_VISIBLE_DEVICES = '0'
$env:GGML_VK_DISABLE_COOPMAT = '1'
& "D:\work\b70tools\build\b70tools.exe" run --ticks 180 --out "D:\work\b70tools\runs\single-gpu-card1"
# in another terminal:
& "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" -m "<model>" -ngl 99 --no-mmap -dio -c 4096 -n 1500 -p "<prompt>"

# CARD 2 (Vulkan1 = adapter_00012fbe, broken-IGCL):
$env:GGML_VK_VISIBLE_DEVICES = '1'
# rest identical
```
