# findings — dual-B70 layer split, Qwen2.5-32B dense Q4_K_M

**Run:** `D:\work\b70tools\runs\dual-b70-qwen25-32b-q4-1\events.jsonl`
**When:** 2026-05-28, ~06:46 PDT.
**Build state:** B (post-airflow tweak, still workbench).
**Workload:** `qwen2.5-32b-instruct-q4_K_M.gguf` (~18.5 GB dense Q4) via `llama-cli.exe`. Both Vulkan devices visible. Layer split: `-sm layer -ts 1,1 -fit off`. `-n 2000` tokens.
**Telemetry duration:** 606.3 s wall (600 ticks + jitter). b70tools started 12 s before llama-cli per the cold-start safety pattern.
**Throughput delivered:** **prompt eval 242.2 t/s, generation 20.7 t/s.**
**JSONL:** 1.16 MiB / 4862 lines / 1953 B/s. No new disagreement classes; no cascade; silence rule did not fire.

This is the **dense-comparison run** to the prior MoE experiment — same dual-B70 layer split, same precision (Q4_K_M), same prompt, same context length. The contrast confirms what the operator's intuition predicted about the two workload classes.

---

## Headline result — MoE vs dense, same precision, same rig

| Metric | Qwen3-30B-A3B (MoE) | Qwen2.5-32B (dense) | Delta |
|---|---|---|---|
| **Prompt eval throughput** | 30.1 t/s | **242.2 t/s** | **8.0× faster on dense** |
| **Generation throughput** | **81.7 t/s** | 20.7 t/s | **4.0× faster on MoE** |
| Activity rate (healthy card, render_compute) | 3.3% | **5.7%** | +73% relative — sustained compute |
| Frequency mean (healthy card, when sampled) | 1.967 GHz | **2.250 GHz** | +14% sustained clock |
| Die temp peak (healthy card) | 61 °C | **66 °C** | +5 °C |
| VRAM temp peak (healthy card) | 60 °C | **68 °C** | +8 °C |
| Die temp peak (slot-1 broken-IGCL card) | 61 °C | **67 °C** | +6 °C |
| VRAM temp peak (slot-1 broken-IGCL card) | 64 °C | **74 °C** | +10 °C |
| Fan peak (healthy card) | 1083 RPM | **1370 RPM** | +27% |

**This matches the operator's mental model exactly:**

- **MoE for fast iterative work** — 81.7 t/s generation means quick turnarounds on refinement prompts. The sparse 3B-active-param footprint per token is what produces that throughput.
- **Dense for sustained structured output** — 242 t/s prompt eval is excellent (fully parallelizes long prompts across all dense weights). The 20.7 t/s generation gives consistent per-token quality with no expert-routing variance, but at the cost of generation latency.

The thermal signature follows directly: dense's full per-token compute heats both cards harder and longer than MoE's sparse activation. **The "+5–10 °C across the board" delta on dense is the telemetry signature of "sustained planning workload" vs "fast debugging workload."**

---

## Per-adapter detail

### adapter_00011b4f (healthy IGCL)

- Activity (full 605 s window): **8.2% global, 5.7% render_compute** — about 1.7× the MoE rate.
- Frequency: 400 MHz – 2.800 GHz, **mean 2.250 GHz** when sampled (vs 1.967 GHz mean on MoE). The mean reflects sustained boost during active inference, not just peak.
- Voltage: 0.715–1.060 V (same envelope as MoE — the boost ceiling is hardware-bound, not workload-bound).
- Thermal: 49–66 °C die / 52–68 °C VRAM. The +5/+8 °C delta over MoE peaks is exactly the dense-compute signature.
- Fan: 707–1370 RPM, mean 862. The fan worked harder than under MoE (1083 peak).
- **80 thermal samples** vs 61 under MoE — workload-driven metric churn was higher.

### adapter_00012fbe (broken-IGCL slot-1 card)

- IGCL voltage/frequency/activity-counter still degraded with the same constant broken values — **366 voltage + 366 freq impossibility reports** across the 606 s window (vs 382/382 under MoE — slightly fewer because heartbeat alignment varies).
- **Activity counters: still broken-advancing** (3547% global, 3985% render_compute, 249% media) — same structural pathology.
- **Thermal credible** — 67 °C die peak, **74 °C VRAM peak**.
- The slot-1 thermal disadvantage held: 74 °C VRAM on this card vs 68 °C on the healthy card = persistent 6 °C delta from slot positioning.
- **Important:** 74 °C VRAM is still well under the cascade-trigger zone you've experienced (State A solo got to 90 °C; State A near-VRAM-limit gets to lockup territory). State B airflow + dual-split workload keep this card 16 °C below its solo-Mistral State A peak.

### IGCL stayed alive throughout — silence rule did not fire

Third consecutive run since shipping the silence rule where it didn't fire (validation #1, validation #2, Qwen MoE, now dense Q4). **Zero false positives across ~26 minutes of cumulative IGCL-healthy operation.** The rule is armed and waiting for the actual failure pattern to recur.

---

## Disagreement profile — unchanged across all dual-B70 experiments

```
[366]  physically_impossible_frequency  /  adapter_00012fbe   span 606 s
[366]  physically_impossible_voltage    /  adapter_00012fbe   span 606 s
  [1]  expected_source_unavailable      /  adapter_00011b4f
```

Three classes — same as idle baseline, same as all single-GPU runs, same as concurrent runs, same as MoE dual-split. **The architecture's noise floor is now confirmed stable across all workload classes tested on this rig.** Any future run that produces a 4th unique class would be a real finding.

---

## Observation cost — fully stable, longest cumulative dataset yet

| | Idle | Solo Mistral | Concurrent | Qwen MoE dual | **Dense Q4 dual** |
|---|---|---|---|---|---|
| Window | 304 s | 182 s | 242 s | 605 s | **606 s** |
| RSS attributed | 16.6 MiB | 16.6 MiB | 16.6 MiB | 16.7 MiB | **16.6 MiB** |
| Vulkan init wall | 51.5 ms | 51.5 ms | 304.5 ms (contention) | 53.0 ms | **51.6 ms** |
| JSONL B/s | 1090 | 1361 | 1992 | 1983 | **1953** |
| Do-no-harm budget | PASS | PASS | PASS | PASS | **PASS** |

Vulkan init back to baseline because b70tools started 12 s before llama-cli (no ICD-init contention). RSS held flat through every experiment — collector inits are stable regardless of workload class.

---

## What this tells us about the upcoming Q6 run (when you re-download it)

The dense Q4 → Q6 step at the same architecture will:

- **Reduce generation throughput further** — Q6 has 50% more bits per parameter than Q4, so memory bandwidth becomes more of a bottleneck per token. Expect ~14–17 t/s generation (down from 20.7 at Q4).
- **Roughly preserve prompt-eval throughput** — Q6 doesn't change the compute parallelism story; we'd expect 200+ t/s prompt eval still.
- **Increase per-card VRAM footprint to ~13.5 GiB** (from 9.3 GiB at Q4 layer-split). Still very safe but the closest to the budget we've measured.
- **Thermal envelope:** similar to or slightly higher than dense Q4. Likely 68–72 °C die peaks, 72–78 °C VRAM peaks on the broken card. Still well below State A solo Mistral territory.

If those predictions land cleanly, the architecture has now characterized all three dual-B70 workload classes at the same precision and one at higher precision — a complete operational baseline for the rig's two use cases.

---

## Suggested next steps

1. **Wait on the Q6 manual download** as planned — gives us the dense + high-quality data point.
2. **Possible longer sustained Q4 dense run** (`-n 8000`+) to push toward thermal equilibrium — the current ~110 s of active workload in a 605 s window mostly shows transient heating. A longer workload would show whether the cards stabilize, drift up, or hit a thermal ceiling.
3. **70B Q4 dual-B70** per your original recipe — direct head-to-head against your 11.7 t/s gen benchmark. Would give us the largest-model data point and the largest sustained per-card residency we can responsibly run.

Standing by for which is most operationally useful next.
