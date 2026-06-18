# WoW realtime inference impact plan

**Branch:** `research-wow-realtime-inference-impact`
**Status:** plan only
**Date:** 2026-05-29

## Question

The machine has two Intel Arc Pro B70 cards, but games cannot use them as one GPU
because there is no SLI-style gaming path. The research question is whether the
second B70 can run realtime or near-realtime inference for Tempo combat-log
coaching without harming World of Warcraft performance on the gaming card.

This is not primarily a model-quality experiment. The first pass should answer:

> Can we use the otherwise-idle second B70 for parser-driven inference while WoW is
> running, with game frame pacing staying inside an acceptable no-harm budget?

## Current evidence

- b70tools already captures per-adapter telemetry, disagreement reports, and
  observer cost at low overhead.
- Single-card B70 inference is characterized: both cards can run Vulkan inference
  at essentially identical throughput.
- Concurrent independent inference on both cards works, but b70tools observed
  Vulkan/IGCL contention and a top-card telemetry silence failure mode.
- Tempo already has a live-tail path: `CombatLogParser` plus
  `FileSystemLogMonitor`, with documented flush-aware behavior.
- Tempo also has B70 llama.cpp Vulkan recipes and parser-anchored research
  harnesses. Existing findings show parser-compressed evidence is much cheaper
  than raw combat-log context.

The missing piece is controlled game-impact measurement, especially frame pacing.
b70tools tells us what the GPUs and observer are doing; it does not currently
record WoW frame times.

## Primary hypothesis

Running a single-card llama.cpp/llama-server workload bound to the non-display B70
can process post-fight or low-duty-cycle realtime combat-log prompts without
materially degrading WoW frame pacing on the display/gaming B70.

## Secondary hypotheses

1. Parser-only live tailing has negligible game impact.
2. Post-fight inference after `ENCOUNTER_END` is safer than continuous in-fight
   inference because the game is between high-combat render bursts.
3. Single-card inference on the second B70 is safer than dual-card layer-split
   inference because dual split necessarily touches the gaming card.
4. Host RAM pressure is the largest non-GPU risk on the current 32 GB system.
5. The safe path probably requires a small or MoE model, parser-compressed
   evidence, request throttling, and explicit GPU binding.

## No-harm budget

The experiment fails if any baseline-normalized condition is exceeded:

| Signal | Initial budget |
|---|---:|
| Game p50 frame time | <= +2% |
| Game p95 frame time | <= +3% |
| Game p99 frame time | <= +5% |
| Long frame count over 50 ms | no material increase |
| WoW crash, driver reset, TDR, or device lost | 0 |
| System free RAM during test | keep > 4 GB |
| Pagefile growth during active play | no sustained growth |
| Gaming-card temperature | no new throttle-shaped plateau |
| Gaming-card render/compute activity | no unexplained sustained increase |

These are starting thresholds. If baseline frame pacing is noisy, compare by
paired run blocks and report confidence rather than forcing a false pass/fail.

## First-pass architecture

Keep b70tools as the experiment owner and Tempo as the workload supplier.

```
WoW advanced combat log
  -> Tempo live parser / evidence projection
  -> request queue with rate limits
  -> llama-server or llama-cli bound to second B70

In parallel:
  -> b70tools run captures per-adapter GPU telemetry
  -> frame-time trace captures WoW frame pacing
  -> harness writes run manifest and timestamps
```

For the first research branch, do not deeply integrate Tempo into b70tools. Use
scripts and manifests first. Only add b70tools code if the analysis needs a
stable first-class metric or event marker.

## Required measurements

### b70tools telemetry

Use existing commands:

```powershell
.\build\b70tools.exe run --ticks <N> --out .\runs\<run>
.\build\b70tools.exe adapters .\runs\<run>
.\build\b70tools.exe summarize .\runs\<run>
.\build\b70tools.exe disagreements .\runs\<run>
.\build\b70tools.exe self .\runs\<run>
```

Capture at least:

- adapter identity and which B70 is display/gaming vs inference
- per-adapter thermal envelopes
- render/compute activity where credible
- init and observer cost
- silent-source/disagreement behavior

### Game frame pacing

Add an external frame-time capture to the experiment manifest. Candidate tools:

- PresentMon CLI, if available locally
- CapFrameX export, if already installed
- WoW built-in FPS sampling only as a fallback, because averages are not enough

The plan should prefer PresentMon-style frame events because the decision hinges
on p95/p99 and long-frame counts, not average FPS.

### Host pressure

Capture once per second:

- total/free RAM
- pagefile committed bytes
- WoW process CPU/RSS
- inference process CPU/RSS
- disk active time if pagefile grows

This can be a small PowerShell sidecar in phase 1; it does not need to be a
b70tools collector unless the experiment repeats often.

### Inference QoS

For each inference request:

- trigger source: periodic, encounter start, encounter end, wipe, manual replay
- evidence token estimate or byte size
- prompt-eval time
- generation time
- total latency
- tokens/sec
- model, context, GPU binding, llama.cpp build
- whether result landed before the next pull or within the desired coaching SLA

## Workload matrix

Run in this order. Stop if a step violates the no-harm budget.

| Step | Scenario | Purpose |
|---:|---|---|
| 0 | Idle desktop, no WoW, no inference | Verify telemetry and frame tooling are quiet |
| 1 | WoW only, same route/raid/dummy pattern | Game frame baseline |
| 2 | WoW + b70tools only | Observer cost against actual game |
| 3 | WoW + Tempo parser only | Parser/live-tail cost |
| 4 | WoW + parser + post-fight inference on second B70 | Main target path |
| 5 | WoW + parser + periodic in-fight low-duty inference | Stress realtime coaching path |
| 6 | WoW + inference on gaming/display B70 | Negative control |
| 7 | WoW + dual-card layer-split inference | Negative control, expected to risk impact |
| 8 | WoW + intentionally oversized context/model | Host-RAM/pagefile failure boundary |

## GPU binding rules

Initial safest inference binding:

```powershell
$env:GGML_VK_VISIBLE_DEVICES = '1' # or whichever adapter is not driving WoW/display
$env:GGML_VK_DISABLE_COOPMAT = '1'
```

Rules:

- Single-card inference only for the first pass.
- Do not use `GGML_VK_VISIBLE_DEVICES=0,1` for the primary hypothesis.
- Do not use dual-card layer split except as an explicit negative control.
- Use `--no-mmap -dio` for large GGUF loads.
- Use `-fit off` only when intentionally testing layer split; otherwise avoid
  ambiguous auto-fit behavior.
- Preload the model before starting the measured game segment when testing
  steady-state impact. Separately measure model-load impact as its own case.

## Candidate model tiers

Start with models already characterized in Tempo/b70tools research:

| Tier | Candidate | Why |
|---|---|---|
| Small fast path | qwen2.5 14B Q4 | Low risk, useful negative/positive control |
| Main path | Qwen3-30B-A3B Q4 MoE | Good speed/quality tradeoff, fits single card |
| Heavy path | Mistral Small 24B Q4 | Known single-card B70 signature |
| Avoid first | 70B dual split | Uses both cards; not the primary question |

The first answer should be based on the smallest model that can deliver useful
coaching shape, not the largest model the hardware can run.

## Prompt/evidence policy

Use parser-compressed evidence, not raw combat-log chunks, for first-pass tests.

Preferred request shapes:

1. `ENCOUNTER_END` post-fight summary: one bounded prompt per boss pull.
2. Wipe-specific coaching: one bounded prompt on `success=0`.
3. Periodic in-fight pulse: at most one small request every 15-30 seconds, with
   stale-request dropping if the previous inference is still running.

Hard limits for phase 1:

- no unbounded raw-log streaming into the model
- no concurrent request pileups
- no more than one active inference request
- drop stale work rather than queueing into the next pull

## Experiment harness deliverables

Phase 1 should produce scripts and docs, not a large app:

1. `docs/wow-realtime-inference-impact-plan.md` - this plan.
2. `scripts/wow-impact/Start-WowImpactRun.ps1` - starts b70tools, optional
   frame capture, host-pressure capture, and writes a manifest.
3. `scripts/wow-impact/Invoke-InferenceStimulus.ps1` - sends controlled prompts
   to llama-server or runs llama-cli with a fixed prompt file.
4. `scripts/wow-impact/Summarize-WowImpactRun.ps1` - joins frame stats,
   b70tools summaries, host-pressure stats, and inference timings.
5. `docs/findings-wow-realtime-inference-impact-1.md` - first results document.

Initial script harness added under `scripts/wow-impact/`; see
`scripts/wow-impact/README.md` for usage.

Only add C++ b70tools features after the scripts prove a repeated need. Likely
candidate later additions:

- `b70tools mark <run> <label>` for timestamped experiment markers
- host-pressure collector
- frame-time import/summarize command
- source-went-silent rule if not already implemented on the active branch

## Run manifest

Each run directory should contain:

```text
manifest.json
events.jsonl
b70tools-adapters.txt
b70tools-summary.txt
b70tools-disagreements.txt
b70tools-self.txt
frame-times.csv
host-pressure.jsonl
inference-requests.jsonl
notes.md
```

Minimum `manifest.json` fields:

- run id and timestamp
- WoW mode/location: raid, target dummy, city, replay/synthetic
- display/gaming adapter id
- inference adapter id
- model path/name
- llama.cpp build
- GPU binding environment
- Tempo commit or parser build source
- b70tools commit
- driver version
- Windows build
- test step number from the workload matrix

## Analysis method

Compare each scenario against the nearest WoW-only baseline from the same session.

For frame pacing:

- p50/p95/p99 frame time
- average FPS only as secondary context
- long frame count over 33.3 ms, 50 ms, and 100 ms
- worst 1-second and 5-second windows

For GPU telemetry:

- gaming-card thermal and activity deltas
- inference-card thermal and activity deltas
- evidence that inference stayed isolated to the second card
- new disagreement classes
- IGCL silence or missing-source behavior

For host pressure:

- minimum free RAM
- pagefile delta
- disk active time if pagefile grows
- process RSS deltas

For inference:

- latency distribution
- throughput
- queue depth/drop count
- whether output arrived before it became stale

## Decision rules

### Green

- Frame-time p95 and p99 stay within budget.
- No new long-frame cluster is temporally aligned with inference.
- WoW/display card telemetry does not show sustained compute/thermal disturbance.
- Free RAM stays above 4 GB with no sustained pagefile growth.
- Inference returns useful post-fight output before the next pull most of the time.

### Yellow

- Average FPS is fine but p99/long-frame count worsens.
- Only model-load causes stutter, but preloaded steady state is clean.
- Host RAM margin is tight but not paging.
- Inference is safe only at low duty cycle.

### Red

- Any TDR/device lost/crash.
- Sustained paging or free RAM below 2 GB.
- p99 frame time or long frames materially worsen during inference.
- Inference leaks onto the gaming card when it should be isolated.
- Dual-card layer split is required for acceptable output quality.

## Expected first finding

The likely viable shape is:

- WoW on the display/gaming B70.
- llama-server preloaded on the second B70.
- parser-compressed prompt after `ENCOUNTER_END`.
- one active request at a time.
- stale work dropped.
- small/MoE model first.

The likely non-viable shapes are:

- dual-card layer split during active gameplay
- loading a large model mid-fight
- raw-log large-context prompts
- two concurrent llama-server processes on the current 32 GB host RAM
- any path that causes Windows shared-GPU-memory spillover or pagefile churn

## Immediate next step

Build phase 1 scripts around existing tools, then run the matrix through step 4:

1. WoW-only baseline.
2. WoW + b70tools.
3. WoW + parser only.
4. WoW + parser + post-fight inference on the second B70.

If step 4 is green, proceed to periodic in-fight inference. If step 4 is yellow,
optimize request size and model choice before trying real-time coaching.

## Round 1 result

First live 5-player test captured in
`docs/findings-wow-realtime-inference-impact-1.md`; retrospective in
`docs/retrospective-wow-realtime-inference-impact-2026-05-29.md`.

## Round 2 — overnight regression + post-driver baseline (2026-06-16)

Three-repo regression harness + an unattended 7-model GPU sweep. Plan + results:
`docs/overnight-regression-plan-2026-06-16.md`; queued follow-ups:
`docs/inference-test-backlog.md`; retrospective:
`docs/retrospective-wow-realtime-inference-impact-overnight-2026-06-16.md`.

Headlines: parser + both math layers (Tempo percolation, leopard RaidUI ports) green on the
new post-patch corpus; GPU throughput numbers **contaminated by concurrent host load** (uniform
~200 prefill / ~5.7 decode clamp across all 7 models) → driver exonerated, clean-room re-run
required. New rig findings: shared-GPU-memory spillover observed live on the 70B (b70tools' PDH
signal caught the 6 GB spill); the real host wall is **commit charge, not free RAM** (92% commit
vs 60% physical) → verdict-gate should watch commit headroom. MoE chosen as the single-card
always-hot local-tooling backend.

## Round 3 — driver fix verified + SYCL decode unlock (2026-06-18)

Investigation session. Retrospective:
`docs/retrospective-bsod-fix-and-sycl-unlock-2026-06-18.md`; follow-ups appended to
`docs/inference-test-backlog.md` (#8); new harness `eval/scripts/bench-config.ps1`.

Headlines: the load-test BSOD (`0xD1` in `igfxnd`, desktop/mode-switch-under-load) is **fixed
by driver `8801` → `8826`**, verified under exact original crash conditions (overlay + dual-card
load, both `Win+Shift+S` and `Ctrl+Alt+Del`, no crash); the `hud.exe` overlay is exonerated.
Long-context decode on Vulkan root-caused to the **attention kernel** — coopmat, card count, KV
format, contention, and the shared-memory-spill theory all ruled out (the last by a live PDH
probe: clean card 22.78 GB dedicated / 0.49 shared, no spill, still 4.2 t/s). **IPEX-LLM/SYCL
decodes 3.5× faster at 25k** (14.47 vs 4.17 t/s, same 32B, n=4 <1% variance), using the
ipex-llm-ollama stack already on the box. Decision rule: **long context → SYCL, short → Vulkan**;
planner/critic one-model-per-card (each fits one 32 GB card) is the measured rig optimum.
Capacity correction: each B70 is 32 GB VRAM (64 GB pooled), not "48/card."
