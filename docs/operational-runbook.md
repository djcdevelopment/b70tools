# b70tools — Operational runbook

**Status:** in use. Idle baseline capture under way; single-GPU and dual-B70 inference experiments are next.
**Date:** 2026-05-28.
**Audience:** the operator at the rig (the user); also future-Claude/Codex sessions.

This runbook describes the three experiments that take b70tools from "implementation done" to **operational understanding of Windows-native Vulkan inference**. The collector set is frozen at v1's four collectors (D3DKMT, DXGI VMI, Vulkan budget, IGCL); no new collectors are planned until these runs surface something the existing set can't answer.

---

## Pre-flight check

Run once before any experiment:

```powershell
& "D:\work\b70tools\build\b70tools.exe" --enumerate --out "D:\work\b70tools\runs\preflight"
```

Inspect the printed summary or:

```powershell
& "D:\work\b70tools\build\b70tools.exe" summarize "D:\work\b70tools\runs\preflight"
```

Pass criteria:

- Both `adapter_00011b4f` and `adapter_00012fbe` are reconciled (`Adapters (2)`).
- DXGI / SetupAPI / Vulkan all reported OK.
- No `ambiguous` warning.
- Driver shows `32.0.101.8801` (decoded from UUID).

**Known limitations** to remember while interpreting any run (already in rig-baseline memory):

- D3DKMT `KMTQAITYPE_ADAPTERPERFDATA` returns `STATUS_INVALID_PARAMETER` on this driver path. The `expected_source_unavailable` disagreement is expected; not a problem.
- `adapter_00012fbe` IGCL voltage/frequency are structurally implausible (`5.117 V`, `8.55 GHz` at idle). v1.5 rules now flag these as `physically_impossible_voltage` / `physically_impossible_frequency` every tick. Raw values preserved.
- WDAC self-detect reports `false`; actually Enforced. Known fingerprint bug.
- `RTSSVkLayer64.dll` (RivaTuner) intercepts Vulkan on this host — captured by audit; expected.
- PCIe link gen/width via SetupAPI display devnode is unreliable; not currently emitted as MetricSamples.

These are baseline noise. New disagreements from the inference runs should rise above this floor to be considered findings.

---

## Experiment 1 — Idle baseline (running now)

**Goal:** quantify the noise floor. Clock drift, residency drift, temperature equilibrium, disagreement frequency, collector stability, observation cost stability — all at idle.

**Procedure:**

```powershell
& "D:\work\b70tools\build\b70tools.exe" run --ticks 300 --out "D:\work\b70tools\runs\baseline-idle-1"
```

- 300 ticks × 1 s ≈ 5 minutes wall time.
- Normal desktop allowed: browser, IDE, music, Defender activity.
- **No inference workload running.** No `python.exe`, no `llama-server.exe`, no `ggml-vulkan-test`, etc.
- Ctrl-C is now clean: poll loop sees the flag, exits, JSONL flushes. Max delay = one cadence (~1 s).

**Analysis (three CLI verbs now available):**

```powershell
& "D:\work\b70tools\build\b70tools.exe" summarize     "D:\work\b70tools\runs\baseline-idle-1"
& "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\baseline-idle-1"
& "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\baseline-idle-1"
& "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\baseline-idle-1"
```

- `summarize` — the structured headline report (everything, grouped).
- `adapters` — per-adapter detail with **computed activity rates** (`Δcounter / Δwall` → % activity) and memory/clock/voltage envelopes. **The headline-answer verb for inference experiments**: did this card's activity rate rise above baseline?
- `disagreements` — focused per-(rule, adapter) view, sorted by frequency.
- `self` — observation-cost summary + do-no-harm budget check.

**What to look for:**

| Question | Expected at idle |
|---|---|
| Did both adapters reach a state past `Unknown`? | Yes → `Idle` for both. |
| Peak `gpu.frequency_hz` (adapter_00011b4f) | ≈ 400–800 MHz (idle clock). |
| Peak `gpu.temperature_c` | ≈ 45–60 °C (room + idle dissipation). |
| Peak `vram.local.current_usage_bytes` | Small (KiB–MiB), no growth pattern. |
| Disagreement count | Steady (D3DKMT-unavailable + impossible-voltage/frequency on adapter_00012fbe each tick). |
| Observation cost: RSS delta | Stable, no growth across the 300 ticks. |
| JSONL bytes/event | Should hold near ~250 B due to delta-suppression. |
| Heartbeat snapshots | 1 every 30 s → ~10 across the 5 min run. |

**Pass criteria:**

- Both adapters stayed `Idle` (no false transitions to `Awake`/`ActiveCompute`).
- No new disagreement classes appeared beyond the known baseline noise.
- Observation cost stable; RSS not climbing.
- No collector got disabled by the watchdog.

Output: `runs/baseline-idle-1/events.jsonl` (the recording) + `docs/baseline-findings-idle.md` (written after summarize).

---

## Experiment 2 — Single-GPU inference

**Goal:** establish expected wake/activity semantics on **one card**. Useful as a reference for the two-card case.

**Procedure:**

1. Start the telemetry run in one terminal (long enough for the inference to complete):
   ```powershell
   & "D:\work\b70tools\build\b70tools.exe" run --ticks 0 --out "D:\work\b70tools\runs\single-gpu-1"
   ```
   `--ticks 0` = unlimited. Ctrl-C when the workload completes.

2. In another terminal, run a known-good Vulkan inference workload pinned to **one B70**. Pattern (the operator adapts to whatever workload is on the rig):
   - **llama.cpp Vulkan example** (one device):
     ```powershell
     # adjust paths / model to whatever you have locally
     & "D:\path\to\llama-server-vulkan.exe" `
         -m "D:\models\<some-Q4-32B-or-13B>.gguf" `
         -ngl 999 `
         --device "Intel(R) Arc(TM) Pro B70 Graphics" `
         --main-gpu 0 `
         -c 4096
     # then drive a chat / completion through the local API for 30–60 s
     ```
   - Choose model size that comfortably fits one 32 GB card (≤ ~24 GB to leave headroom).
   - Run for at least one minute of active token generation so activity counters move noticeably.

3. Stop the workload, Ctrl-C the b70tools `run`.

4. Analyze (three verbs — start with `adapters`):
   ```powershell
   & "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\single-gpu-1"
   & "D:\work\b70tools\build\b70tools.exe" summarize     "D:\work\b70tools\runs\single-gpu-1"
   & "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\single-gpu-1"
   & "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\single-gpu-1"
   ```

   The `adapters` verb is the **primary signal** — its computed activity-rate per adapter is the cleanest "did the workload actually exercise this card?" answer. Baseline rate is ~3–6%; **a working single-GPU inference shows the used card's `render_compute` rate at ~30% (≥ 5× baseline) for full-window measurement** (load + active gen averaged together). Thermal delta of +20–25 °C on `gpu.temperature_c` is an even stronger signal — see `docs/findings-single-gpu-mistral24b-1.md` for the empirical reference numbers.

**What to look for:**

| Question | Expected on a healthy single-GPU run |
|---|---|
| AdapterState reached on used B70 (likely `adapter_00011b4f` if `--main-gpu 0`) | `Awake`, ideally `ActiveCompute` |
| AdapterState reached on idle B70 | Remains `Idle` |
| `gpu.frequency_hz` on used card | Boosts above idle (≥ 1.5 GHz typical) |
| `gpu.temperature_c` on used card | Rises by several °C over the run |
| `gpu.activity.render_compute_counter` delta over the run | Substantial — seconds of active time |
| `vram.local.current_usage_bytes` peak on used card | Grows to whatever the model loads (`-ngl 999` ≈ full weights) |
| `vulkan.heap0.usage_bytes` (our process) | Tiny — we're the observer, not the workload |
| New disagreements vs. baseline | Hopefully none. If `48gb_pattern` fires unexpectedly: real. |

**Pass criteria for this experiment:**

- Used adapter advances past `Idle` (FSM transitions `Idle → Awake → ActiveCompute`).
- VRAM growth visible in the DXGI VMI / IGCL feeds, attributable to the workload PID's residency (the workload, not us).
- No new disagreement classes beyond baseline noise + this run's expected `Awake`/`ActiveCompute` transitions.

**Open questions this experiment answers** (notes for the findings doc):

- Does IGCL `gpu.activity.render_compute_counter` increase under real Vulkan compute? Or is it stuck for non-Intel-OneAPI workloads?
- Does the FSM correctly advance to `ActiveCompute`, or does our current "first evidence of Power/Frequency/Memory" rule jump straight to `Awake` and stay there?
- Does Task Manager's "GPU" column register the workload? (Compare manually.)

---

## Experiment 3 — Dual-B70 split inference (**the important one**)

**Goal:** explain — with arbitrated telemetry — what happens when llama.cpp Vulkan splits a model across both cards. This is the workload that **previously produced confusing Task Manager behavior**, so it's the canonical pathological case b70tools exists to clarify.

**⚠ Operational hazard (recorded from operator's lived experience):** the worst case on this rig — cold-start multi-card split + workload near both cards' VRAM ceiling — can cascade into a near-total system lockup: one card unresponsive in perf monitor, Win UI extremely slow, GPUs still emitting heat. **Recovery from this state often involves a non-POST reboot, memory retraining, GPU retraining, or BIOS reflash (multiple hours).** Two operational rules follow:

- **Start b70tools FIRST**, give it 10-15 s to baseline both cards in cold state, THEN start the workload. This captures the cold→active transition where the failure mode typically begins.
- **Until v1.5 PDH `GPU Process Memory` lands**, do NOT run intentional max-VRAM stress tests. We currently have no telemetry signal for "workload approaching the budget" on this rig — DXGI VMI and Vulkan budget are per-process and show 4 KiB for b70tools itself. Pick model+context sizes with comfortable headroom (e.g. Qwen3-30B-A3B Q4 at 17 GB layer-split = ~8.5 GB per card on 32 GB cards, very safe).

**Procedure:**

1. Telemetry in one terminal:
   ```powershell
   & "D:\work\b70tools\build\b70tools.exe" run --ticks 600 --out "D:\work\b70tools\runs\dual-b70-1"
   ```
   600 ticks = 10 min. Adjust as needed; longer is safer for cascade analysis.

2. Dual-GPU workload in another. Pattern for llama.cpp Vulkan:
   ```powershell
   & "D:\path\to\llama-server-vulkan.exe" `
       -m "D:\models\<larger-model-Q4-or-Q5>.gguf" `
       -ngl 999 `
       --split-mode layer `
       --tensor-split 50,50 `
       -c 8192
   # drive a chat through it for ≥ 2 minutes
   ```

3. Stop workload, Ctrl-C b70tools.

4. Analyze:
   ```powershell
   & "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\dual-b70-1"
   & "D:\work\b70tools\build\b70tools.exe" summarize     "D:\work\b70tools\runs\dual-b70-1"
   & "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\dual-b70-1"
   & "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\dual-b70-1"
   ```

   For the dual case, `adapters` is **the** report — its activity-rate column directly answers whether both cards woke. Both cards' `render_compute` rate well above baseline = success. One card's rate elevated while the other stays at idle = the workload is not actually splitting.

**The six explanatory questions** (from the project's framing):

| Question | What "explained" looks like in the summarize output |
|---|---|
| **Do both adapters wake?** | Both `state reached: Awake` (or higher). The state-transition records carry `reason` showing what evidence advanced each. |
| **Is the layer split actually 50/50?** | `vram.local.current_usage_bytes` peak comparable on both adapters. Significant skew is a finding. |
| **Is the workload Vulkan-bound or PCIe-bound?** | Compare IGCL activity counters between adapters; if both saturated with low Vulkan budget growth, it's compute. If activity asymmetric, one is starved. |
| **Where does Task Manager lie?** | Task Manager's GPU% likely shows only the busiest engine (per plan §A.10). Our PDH-lite metric, if enabled (it's conditional T1; currently off), would sum properly. For v1, the comparison is: Task-Mgr-GPU% vs. IGCL render_compute_counter delta. |
| **Did anything stall, retreat, or TDR?** | New `PostTDR` / `Lost` / `Reenumerating` AdapterStateTransitions. None expected. If they appear: real and important. |
| **Did the impossible-voltage/frequency rules stay only on adapter_00012fbe?** | Yes baseline; if adapter_00011b4f now also fires these, IGCL is destabilizing under load — a major finding. |

**Pass criteria for the milestone "first successful real dual-B70 operational telemetry session"**:

1. b70tools runs uninterrupted through the entire workload.
2. Both adapters advance past `Idle`.
3. Summarize report cleanly explains memory growth on both cards.
4. All disagreement reports are explainable (no unexplained classes).
5. Observation cost stays within the do-no-harm budget (RSS < 50 MB, no perturbation that the user can subjectively notice).

If 1–5 all pass: **M3 milestone hit** — b70tools is operationally useful for understanding multi-GPU Vulkan inference on this rig.

---

## Experiment 4 — Comparison (after the above three exist)

Only build the `compare` verb after we have `baseline-idle-1` + `single-gpu-1` + `dual-b70-1` recordings. The verb's job:

- Fingerprint diff between runs.
- Per-adapter VRAM peak diff (idle vs. single vs. dual).
- Per-adapter activity counter delta diff.
- AdapterState transition diff.
- Disagreement-class diff (which rules fired uniquely in each run).
- Observation cost diff.

`compare baseline-idle dual-b70` should make the "did the workload do what we expect" question answerable in one screen.

---

## Workload selection notes (for the operator)

The runbook assumes llama.cpp Vulkan because:

- It's a known-good Windows + Vulkan + multi-GPU stack.
- It's the workload most associated with the original Task-Manager-disagreement reports that motivated b70tools.
- `--split-mode layer --tensor-split 50,50` is a clean, well-defined dual-GPU pattern.

Substitutes are fine if the operator prefers — vLLM-Vulkan, mlc-llm-Vulkan, Llama.cpp + llama-server, koboldcpp-Vulkan. The telemetry interpretation is the same regardless of which Vulkan inference engine drives the workload.

What we explicitly want **for Experiment 3**: a model large enough that a single 32 GB B70 can't hold it comfortably (so the split is real), but small enough that both cards combined have plenty of headroom (so we observe a normal split, not a memory crisis). Something around 60–100 GB of model weights post-quantization is the sweet spot for this rig.

---

## Stopping a run cleanly

Ctrl-C in the terminal running `b70tools run` sets the global stop flag. The poll loop notices on the next iteration and exits. JSONL writer flushes its buffer. Worst-case delay ≈ one cadence (1 s).

If `b70tools` is wedged (it shouldn't be, but watchdog disables a stuck collector at session level only), kill via:

```powershell
Stop-Process -Name b70tools -Force
```

The buffered JSONL after the last flushed event will be lost. Use only when the clean shutdown fails.

---

## What's NOT in v1 operating mode (intentionally)

- No live TUI. The Phase 2 of the CLI/TUI plan adds `watch`; not yet.
- No live disagreement alerts. The summarize report after the fact is the surface.
- No multi-session aggregation (`compare` will be Phase 4 once we have data).
- No per-process VRAM attribution (would need PDH `GPU Process Memory` full set — v1.5 collector).
- No ETW / PresentMon. v2.

The runbook is intentionally narrow. v1's job is to make the dual-B70 case **legible** with the four collectors we have; expansion follows from real findings, not anticipated need.
