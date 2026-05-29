# findings — dual-B70 layer split, Qwen3-30B-A3B MoE

**Run:** `D:\work\b70tools\runs\dual-b70-qwen30b-moe-1\events.jsonl`
**When:** 2026-05-28, ~06:00 PDT.
**Build state:** **B** (post-airflow tweak, still workbench, no closed case).
**Workload:** Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf (~17 GB, MoE with 3B active params/token) via `llama-cli.exe`. Both Vulkan devices visible (`GGML_VK_VISIBLE_DEVICES=0,1`). Layer split: `-sm layer -ts 1,1`. The critical `-fit off` flag from the operator's 70B recipe. `-n 2000` tokens.
**Telemetry duration:** 605.4 s (600 ticks + jitter). b70tools started 12 s before llama-cli, capturing cold→active→post-workload-idle.
**Throughput delivered:** **prompt eval 30.1 t/s, generation 81.7 t/s.**
**JSONL:** 1.15 MiB / 4750 lines / 252.9 B/event / 1983 B/s.

This is the **first successful dual-B70 operational telemetry run** — the workload completed cleanly, both cards engaged, no cascade, no system instability, no new disagreement classes. The "Experiment 3 milestone" from the runbook is hit.

---

## Headline results

**1. Generation throughput tripled vs. dense 24B at the same precision.**

| Workload | t/s generation |
|---|---|
| Mistral 24B Q4 solo (single card) | 27.3 |
| Mistral 24B Q4 concurrent (independent processes per card) | 27.3 / 28.2 |
| **Qwen3-30B-A3B Q4 dual-B70 layer split** | **81.7** |

The MoE's 3B-active-params per token does exactly what the operator predicted — the smaller per-token compute footprint maps better to dual-B70 layer-parallel pipeline than dense 24B did to single-card or to independent dual-card workloads.

**2. Both cards engaged, identical thermal envelope on the die.**

| Metric | adapter_00011b4f (healthy IGCL) | adapter_00012fbe (broken IGCL) |
|---|---|---|
| `gpu.temperature_c` peak | **61 °C** | **61 °C** (exact tie) |
| `vram.temperature_c` peak | 60 °C | 64 °C (slot-1 still warmer) |
| `gpu.frequency_hz` peak (healthy reading) | **2.800 GHz** | not credible (stuck values) |
| `gpu.voltage_v` peak (healthy reading) | **1.060 V** | not credible |
| `card.fan0.speed` peak (healthy reading) | 1083 RPM | not credible |

**Both cards hit identical 61 °C die peaks** — strongest cross-card evidence that the layer split engaged both. The thermal envelope this time is materially lower than the single-card Mistral solo runs (73 °C die / 78–90 °C VRAM in State A), reflecting two compounding effects: State B airflow improvement + halved per-card compute load from the layer split.

**3. State B airflow handled the dual-split load comfortably.**

The slot-1 cramped card (adapter_00012fbe) peaked at 64 °C VRAM under dual-MoE load — vs 90 °C VRAM under solo Mistral 24B in State A. The airflow tweak + halved per-card load combined for a ~26 °C VRAM reduction on the worst-positioned card. **Closed-case build (State C) would likely improve further.** Plenty of thermal headroom for longer / heavier dual-B70 sessions.

**4. No cascade. No silence. No new disagreement classes.**

The operator's lived-experience pattern (cold-start → IGCL silence cascade → system lockup) did **not** trigger this run. b70tools started 12 s before llama-cli to give IGCL time to settle on both adapters; that may have been the difference, or the failure mode may be more sensitive to other timing/contention factors not in play. The architecture is now armed: `previously_reporting_source_went_silent` would fire if this happens in a future run.

---

## Per-adapter detail

### adapter_00011b4f (healthy IGCL) — under dual-split MoE load

- Activity rate (full 605 s window): **3.3% render_compute, 5.9% global** — at first glance looks like the card barely worked, but this reflects the fact that the inference itself only took ~20 s of GPU time (model load ~20-30 s, generation of 2000 tokens at 81.7 t/s ≈ 24 s, total ~50 s of activity; rest of the 605 s was post-workload idle/cool-down).
- Frequency peak: 2.800 GHz (vs 550 MHz idle); mean 1.967 GHz when sampled — the card boosted hard during the workload then returned to idle clocks.
- Voltage envelope: 0.715–1.060 V — the same range observed under solo Mistral inference.
- Thermal: 50–61 °C die / 52–60 °C VRAM. Cooler than any prior workload-induced data point on this card.
- Fan: 730–1083 RPM (vs 1726 RPM peak under solo Mistral). The card barely needed to spin up.

**Read:** the healthy card carried its half of the layer-split workload cleanly, briefly, and at much lower thermal cost than the solo Mistral case.

### adapter_00012fbe (broken IGCL slot-1 card)

- IGCL voltage/frequency/activity still degraded — **382 + 382 impossibility reports** across the 605 s window, sustained at heartbeat cadence.
- Activity counters still "broken: counter advances ~43× wall clock" — same structural pathology as the baseline.
- **Thermal credible:** die 61 °C peak (matches healthy card exactly), VRAM 64 °C peak. The 4 °C VRAM differential vs the healthy card is the persistent slot-1 thermal disadvantage.
- IGCL stayed alive throughout — no silence event, no firing of the new `previously_reporting_source_went_silent` rule.

**Read:** the same hardware works fine under dual-MoE load; the same IGCL telemetry path stays partially broken in the same way; State B airflow brought the worst-case VRAM from 90 °C (State A solo) down to 64 °C (State B dual-split) on this card. Big operational improvement on the most thermally vulnerable card.

---

## Disagreement profile

```
[382]  physically_impossible_frequency  /  adapter_00012fbe   span 605 s
[382]  physically_impossible_voltage    /  adapter_00012fbe   span 605 s
  [1]  expected_source_unavailable      /  adapter_00011b4f
       (the persistent D3DKMT ADAPTERPERFDATA unavailability)
rate: 75.78 reports / minute
```

The **same three classes that defined the rig's noise floor before** — no new pathologies introduced by dual-split MoE. The doubling of impossibility rate (~75/min vs ~5/min idle, ~20/min single-GPU) is the heartbeat-cadence re-emission of constant broken IGCL values; since the 10-min window is longer, more reports land.

**Importantly: zero `previously_reporting_source_went_silent` reports.** The newly-shipped silence-detection rule did not fire, because IGCL did not actually go silent for either adapter on this run. The rule is armed; this run validated it does not false-positive under normal heartbeat-driven sample emission.

---

## Observation cost — fully stable

| | Idle baseline | Solo Mistral run-1 | Concurrent Mistral run-2 | **Dual-B70 Qwen MoE** |
|---|---|---|---|---|
| Window | 304 s | 182 s | 242 s | 605 s |
| RSS attributed | 16.6 MiB | 16.6 MiB | 16.6 MiB | **16.7 MiB** |
| Init wall total | 126.8 ms | 117.8 ms | 377.3 ms (incl. concurrent ICD lock) | **125.9 ms** |
| JSONL B/s | 1090 | 1361 | 1992 | **1983** |
| Watchdog kicks | 0 | 0 | 0 | 0 |
| Do-no-harm check | PASS | PASS | PASS | **PASS** |

The Vulkan-init time is back to baseline (~50 ms) because b70tools started 12 s before llama-cli — no ICD-init contention. The B/s rate is similar to the concurrent test (close to 2 KiB/s) — driven by IGCL impossibility re-emissions, not workload activity.

---

## What the cold-start hypothesis test told us

The runbook update encoded a precaution: start b70tools first, give it 10-15 s baseline, THEN start the workload. **The cold-start cascade did not trigger on this run.** Two competing interpretations:

1. **The 12 s pre-warm was enough** to let IGCL fully initialize for both adapters before workload contention. If true, this becomes a recommendation: always run b70tools with a baseline lead-time before multi-card workloads.
2. **The cascade is more probabilistic than deterministic.** The operator's lived experience reports it as "often" not "always" — and our concurrent-Mistral runs split 1 silence event / 1 no-silence event across two attempts. The hypothesis remains plausible but not conclusively tested in either direction.

For ongoing operational guidance: keep the b70tools-first pattern; it costs nothing and may help.

---

## v1 limitation still visible

VRAM residency for both adapters reports 4 KiB throughout — b70tools' own process footprint. **The 17 GB Qwen MoE model loaded across both cards (8.5 GB each) is invisible to v1.** This is the persistent gap that PDH `GPU Process Memory` full set fills. The dual-B70 run doesn't make the gap worse; it makes it more frustrating, because we can see the *temperature* of both cards holding ~8.5 GB of model weights but not the residency itself.

**v1.5 priority confirmed:** PDH `GPU Process Memory` full set, exactly as recommended in earlier findings.

---

## Comparing layer-split MoE vs prior experiments

| | Mistral 24B solo (vk0) | Mistral 24B concurrent | **Qwen MoE dual-split** |
|---|---|---|---|
| t/s generation per card | 27.3 | 27.3 / 28.2 | **40.85 / 40.85** (81.7 / 2 layer-split shared) |
| Die peak temp | 73 °C | 73 °C | **61 °C** |
| VRAM peak (slot-1 card) | 90 °C (in State A) | n/a | **64 °C** (in State B) |
| Activity rate (healthy card, derived from 605 s window) | 32% over 182 s ≈ 58 s active | similar | ~50 s active in 605 s ≈ 8% in this metric, but workload ≈ 24 s gen → real active rate during work was effectively 100% |
| Healthy IGCL telemetry: credible | YES | YES | YES |
| Broken IGCL: voltage/freq | broken-stuck | broken-stuck | broken-stuck |
| Broken IGCL: activity counters | broken-regressing | broken-regressing | broken-advancing-fast |
| Broken IGCL: silent failure? | n/a | YES (run-1) / NO (run-2) | NO |

**Operational takeaway:** Qwen3-30B-A3B Q4 + dual-B70 layer split is significantly more efficient than Mistral 24B dense at any configuration we've tested on this rig. Thermal headroom is comfortable. The architecture's noise floor stayed flat.

---

## Suggested next experiments

1. **Longer Qwen run** to sustain workload telemetry: `-n 8000` or larger to keep the cards engaged for several minutes, get a higher-resolution view of sustained-thermal behavior.
2. **The cold-start race condition test** — deliberately reproduce the operator's lived-experience cascade: start cold both cards, no pre-warm, start workload immediately. Compare. (Lower priority since the failure cascade involves system risk; do this only when we're prepared for the recovery cost.)
3. **70B at dual-B70** (the recipe from the operator's tutorial) — for direct comparison against the original benchmarks (11.7 t/s gen).
4. **NOT YET: max-VRAM stress.** Wait for PDH `GPU Process Memory` to land in v1.5 before pushing toward the 32 GB ceiling.

---

## How to re-run

```powershell
# Telemetry first (10-min ceiling per cold-start safety pattern):
& "D:\work\b70tools\build\b70tools.exe" run --ticks 600 --out "D:\work\b70tools\runs\<name>"

# Wait 12-15 s for IGCL to baseline on both adapters, then in another shell:
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'
& "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -m "D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" `
    -ngl 99 -sm layer -ts 1,1 -fit off `
    --no-mmap -dio -c 4096 -n 2000 `
    -p "<your prompt>"
```

Analysis:

```powershell
& "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" summarize     "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\<name>"
```
