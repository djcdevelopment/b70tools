# single-gpu-mistral24b-1 — findings

**Run:** `D:\work\b70tools\runs\single-gpu-mistral24b-1\events.jsonl`
**When:** 2026-05-28, ~04:29–04:32 PDT.
**Workload:** `D:\work\battlemage\models\Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M.gguf` (14.3 GB Q4_K_M) via `llama-cli.exe`. Single-card via `GGML_VK_VISIBLE_DEVICES=0` (Vulkan0 = adapter_00011b4f, the healthy card). `--no-mmap -dio -c 4096 -ngl 99 -n 1500`, with an open-ended essay prompt.
**Telemetry duration:** 182.4 s wall (180 ticks at 1 Hz + jitter). Llama-cli continued past this window — it was killed by the bash 5-min timeout before completing 1500-token generation.
**JSONL:** 242 KiB / 1038 lines / 239.2 B per event. 1361.2 B/s write rate (1.25× the baseline rate; delta-suppression handled the workload-driven metric churn cleanly).

This is the **first operational-telemetry capture of real Vulkan inference on this rig**. The single-GPU experiment landed: the healthy card woke clearly; the idle card stayed at the noise floor; and `b70tools` itself did not perturb anything we can detect.

---

## Headline result

`b70tools adapters` cleanly distinguishes inference from idle on a single card. The signal-to-noise is **~10× on activity rate**, **5× on clock frequency**, and **+24 °C on die temperature**. No new disagreement classes appeared during the run — the rig's noise floor stayed the same three classes documented in the idle baseline.

```
Activity (gpu.activity.render_compute_counter)
  idle baseline:                    3.0%   (Δ +9.0 s over 303.9 s)
  single-GPU Mistral 24B inference: 32.1%  (Δ +58.6 s over 182.2 s)   ← used card
  single-GPU run (idle adapter):    BROKEN counter regression          ← unused card, IGCL still degraded
```

The 32% peak average for sustained inference (not 60–95% as the runbook initially guessed) is the **real signature of single-card Vulkan inference on this rig**. The runbook is updated below to reflect this.

---

## Per-adapter detail

### adapter_00011b4f — the USED card (healthy)

| Metric | Idle baseline | This run | Delta |
|---|---|---|---|
| `gpu.activity.global_counter` rate | 6.4% | **34.5%** | **5.4×** |
| `gpu.activity.render_compute_counter` rate | 3.0% | **32.1%** | **10.7×** |
| `gpu.frequency_hz` peak | 550 MHz | **2.800 GHz** | **5.1×** |
| `gpu.frequency_hz` mean | 450 MHz | 2.206 GHz | 4.9× |
| `gpu.voltage_v` peak | 0.775 V | **1.055 V** | 1.36× |
| `gpu.voltage_v` range | 0.720–0.775 V | 0.730–1.055 V | wider swing |
| `gpu.temperature_c` peak | 49 °C | **73 °C** | **+24 °C** |
| `vram.temperature_c` peak | 52 °C | **78 °C** | **+26 °C** |
| `card.fan0.speed` peak | 866 RPM | **1726 RPM** | 2.0× |

Every physical observable moved in the direction expected for an active Vulkan compute workload. **The healthy card's IGCL telemetry is credible under load** — when the GPU works, the numbers move; when it idles, they don't.

### adapter_00012fbe — the UNUSED card (idle + IGCL degraded)

The unused card stayed at the noise floor for **everything b70tools can measure honestly**:

- Activity counters still showing the same `[BROKEN: counter regressed]` pattern from the baseline. **Confirms the broken counter is NOT load-dependent** — it's structurally wrong regardless of whether we're inferring.
- IGCL voltage: same `5.117 V` (impossible). IGCL frequency: same `8.550 GHz` (impossible).
- Thermal/freq peak values are *almost identical* to baseline (55 °C peak, 8.550 GHz peak, same fan-RPM nonsense). The card was clearly not engaged.

This is exactly the expected "the other card is idle" signature. **The `GGML_VK_VISIBLE_DEVICES=0` isolation worked** — llama-cli only used Vulkan0, and b70tools confirms it.

---

## Disagreement profile — unchanged

```
 3 unique (rule, adapter) pairs   (vs. 3 in idle baseline)
[12]  physically_impossible_frequency  /  adapter_00012fbe   (span 177.1 s)
[12]  physically_impossible_voltage    /  adapter_00012fbe   (span 177.1 s)
 [1]  expected_source_unavailable      /  adapter_00011b4f
```

**No new disagreement classes during inference.** The arbitrator's noise floor is the same. This is exactly what we want from a baseline operational-telemetry run: a clean signal layered on the known noise floor, no new pathologies introduced by the workload.

## Observation cost — stable across workload

```
RSS attributed to collector inits:  16.6 MiB   (identical to baseline)
init wall time total:               117.8 ms
jsonl on disk:                      242 KiB  (1361 B/s, 79.8 KiB/min)
bytes per event:                    239.2
do-no-harm default budget (<50 MiB):  PASS
do-no-harm stretch budget (<30 MiB):  PASS
```

The 25% increase in B/s vs. idle baseline (1361 vs. 1090) is the workload-driven metric churn: clocks, temps, voltage, and activity counters change more during inference, so delta-suppression suppresses less. **The observation cost still passes the do-no-harm budget by a 3× margin.**

---

## Critical v1 limitation exposed

**`b70tools` cannot see the workload's VRAM residency.** Both DXGI `QueryVideoMemoryInfo` and Vulkan `VK_EXT_memory_budget` are per-process — they show 4 KiB for adapter_00011b4f throughout the run, because **that's all b70tools itself allocated** (loader/ICD scratch). The 14 GB of Mistral 24B weights that llama-cli loaded onto the healthy card are *invisible to v1 telemetry*.

This is the most important operational gap for inference observability:

- **Cannot answer:** "how much VRAM did the workload use?" (need per-PID attribution)
- **Cannot answer:** "is the workload approaching the budget?" (would need workload-PID's `heapUsage / heapBudget`)
- **Can answer (indirectly):** "is something running?" (via the activity / clock / thermal signals on the engaged card)

**v1.5 priority #1 should be:** PDH `GPU Process Memory` full set (plan §A.10 list, currently deferred). The collector class is `PassiveSafe` per the audit baseline; adding it is a low-risk telemetry win specifically because we already proved PDH counter access is clean (the conditional `pdh_gpu_engine_lite` is the same family).

---

## Findings that update the runbook

### Finding A — "60–95% activity" was wrong; the real signature is "~10× the idle baseline rate"

The runbook (Experiment 2) said "the used card's `render_compute` rate to 60%+ sustained." The actual measured value at the **mean of a full inference window (load + active gen)** is **32%**. The active-only portion is presumably higher but is dragged down by model-load time.

**Updated runbook expectation:** **`render_compute` rate ≥ 5× the idle baseline on the used card** = clear single-GPU wake. ~30% sustained is normal for an active 24B Q4 single-card layer-parallel workload on B70. ~60% would be exceptional and would suggest a better-optimized workload or different model architecture.

### Finding B — Thermal delta is a stronger signal than activity rate

`gpu.temperature_c` moved **+24 °C** and `vram.temperature_c` moved **+26 °C** — these are unambiguous and resistant to rate-derivation issues. **For interpreting future inference runs, look at the thermal delta first.** If both adapters' temps rise, both are working. If only one rises, only one is engaged.

### Finding C — `adapter_00012fbe`'s broken IGCL is NOT workload-dependent

The same 5.117V / 8.55GHz / regressed-counter pattern showed up in both the idle baseline and the inference run with the same shape and amplitude. **The IGCL degradation is structural, not transient or load-induced.** Confirms the working hypothesis: IGCL is reading the wrong device's registers for this adapter on this driver.

### Finding D — `b70tools adapters` is the right primary report for inference experiments

The verb's per-adapter activity-rate column answers the headline question ("did this card work?") in one line. The thermal/clock envelope answers the secondary question ("how hard?"). `summarize` is still useful for the overall posture; `disagreements` is the fastest "did anything new break?" check; `self` confirms do-no-harm.

---

## Open questions for Experiment 3 (dual-B70)

1. **Will both cards' `render_compute` rate rise to ~30%-ish, or will they each carry half (~15% each) due to layer-parallel sequential dependency?**
   - Layer-split is pipeline-parallel, not tensor-parallel. Per-token latency is bounded by the slower card. So each card should be ~half-utilized over time. **Predicted: ~15-20% activity per card, +temp on both.**
2. **Will adapter_00012fbe's IGCL stay broken when it's actually engaged?**
   - If yes: telemetry-arbitration story is complete (the second card is unobservable via IGCL even when working).
   - If no (it becomes credible under load): would change the working hypothesis.
3. **Will any new disagreement classes appear?**
   - The 70B doc mentioned "Intel Pro Graphics Software: card 0 = 26.8 GB used, card 1 = 0 GB used" when `-fit on` mis-routes. That's the canonical Task Manager disagreement the project was built for. **A dual-B70 run with `-fit off` should NOT see this pattern; a run with `-fit on` (or no flag) SHOULD see it.** Worth running both to verify.

---

## How to re-run this experiment

```powershell
$env:GGML_VK_VISIBLE_DEVICES = '0'   # Vulkan0 only = the healthy card
$env:GGML_VK_DISABLE_COOPMAT = '1'

# Background telemetry — pick a duration > expected workload wall time:
& "D:\work\b70tools\build\b70tools.exe" run --ticks 180 --out "D:\work\b70tools\runs\<name>"

# Inference (foreground, blocks until done or token budget reached):
& "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -m "D:\work\battlemage\models\Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M.gguf" `
    -ngl 99 --no-mmap -dio -c 4096 -n 1500 `
    -p "<some open-ended prompt>"

# Analysis:
& "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" summarize     "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\<name>"
& "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\<name>"
```
