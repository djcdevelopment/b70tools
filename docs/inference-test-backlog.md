# Inference test backlog — to run later (tomorrow night / later this week)

Created 2026-06-16 from the overnight run. Context lives in
`docs/overnight-regression-plan-2026-06-16.md` (the "FINDING: host contention" section). These
are the follow-ups we identified but did NOT run tonight. Ordered by value. Tick as you go.

> **Golden rule learned tonight:** for any number meant to be *comparable*, **quiesce the box
> first** — close background apps, stop other agents. Tonight's −46% prefill drop was host RAM
> contention (18% → 38–54%), not the driver and not thermal (GPU clocks were *higher*, temps
> unchanged). A "busy-box" number is fine for feel, useless for A/B.

---

## 1. Single-card (x16) vs dual-card (x8/x8) MoE — the PCIe bifurcation test  ⭐ highest value
**Question:** how much does the board's x8/x8 dual-card bifurcation + layer-split inter-card
transfer cost vs single-card x16? (Board: PCIe 4.0; 1 GPU = x16 ≈ 32 GB/s, 2 GPU = x8/x8 ≈
16 GB/s each, and `-sm layer` routes inter-card activations through host RAM.)
**Prediction:** single-card MoE prefill *beats* dual-card despite one fewer GPU (no inter-card
transfer, full x16). Decode similar or better.
**How:**
- Dual-card number = tonight's `eval/runs/qwen3-30b-moe-q4-stress-20260616-045138` (already captured).
- Single-card number = start the always-hot MoE (x16) and run the same 70k-prompt eval against it:
  ```powershell
  & 'D:\work\b70tools\scripts\tooling\Start-AlwaysHotMoE.ps1'   # single card, :8090, x16
  # then point an eval at http://127.0.0.1:8090/v1 with the same snapshot-truncated-70k.md
  ```
- **TODO (small):** a post-sweep A/B script that runs the 70k eval against the single-card
  endpoint and diffs prefill/decode vs the dual-card manifest. (Offered; say the word and I'll write it.)
**Prereq:** sweep finished (frees both cards). Quiesce the box.

## 2. Clean-room contention-free baseline  ⭐
**Question:** what's the real contention-free throughput, and how big was tonight's contention penalty?
**How:** quiesce the box completely (no agents, no apps), re-run the stress suite for 2–3 models
(14b + mistral + MoE is enough) and compare to both tonight's busy-box numbers and the 20260605 baseline.
  ```powershell
  & 'D:\work\b70tools\scripts\stress-suite\Start-StressSuite.ps1' -SkipHud -Models qwen2.5-14b-q4,mistral-24b-q4,qwen3-30b-moe-q4
  ```
**Answers:** puts a number on the contention penalty; confirms the driver update is clean.
**Prereq:** box quiesced.

## 3. Always-hot MoE serving validation (the actual use case)
**Question:** does the single-card always-hot MoE work as a real tooling/MCP backend, and what's
the round-trip latency on small (tooling-sized, not 70k) contexts?
**How:** start `Start-AlwaysHotMoE.ps1`, point one MCP server / tool / leopard
(`AskProviderApi="openai"`, base `http://127.0.0.1:8090/v1`) at it, run a few real tool tasks,
note latency + whether it stays hot through the supervisor.
**Prereq:** cards free (after sweep).

## 3b. 70B dual-card split tuning — stop the shared-memory spillover
**Observed (2026-06-16):** the 70B with `-sm layer -ts 1,1 -fit off` split LOPSIDED — GPU 1
hit ~34 GB committed (past its 32 GB dedicated) and **spilled ~6 GB into shared host memory**
(b70tools non_local 6.23 GB ~= Task Manager 5.8 GB shared), while GPU 0 sat at ~26 GB with
headroom. Spilled weights are accessed over PCIe 4.0 x8 -> a severe perf hit on top of the
dual-card penalty. This is the plan's red-line spillover, observed live; b70tools' PDH per-
adapter VRAM signal caught it on both cards (validates [[cross-process-vram-signal]]).
**To try:** rebalance the tensor-split toward the lighter card (e.g. `-ts 0.9,1.1` or similar),
or drop `-fit off` and let llama.cpp auto-fit, so both cards stay inside dedicated VRAM. Re-run
70B and confirm `shared/non_local == 0` in b70tools telemetry + Task Manager. The 70B is the
ONLY model big enough to force this (everything else fits one 28 GB card) -> it's the real
dual-card case and the one worth tuning.
**Prereq:** cards free; quiesce box (per golden rule).

## 3c. Commit-ceiling is the real host wall (not physical RAM) — mitigations to test
**Observed (2026-06-16, 70B run, Task Mgr + b70tools telemetry agree):** physical RAM was fine
(60% used max, 13.5 GB always free, disk 0%), but **commit charge hit 92% of the limit (83.1 /
89.9 GB, 6.8 GB headroom).** The commit is GPU-driven — WDDM backs the ~58 GB dedicated VRAM +
6 GB spill with system commit — plus `--no-mmap` loading the model into committed memory. It's
reserved, not resident (no thrash), but at the limit *allocations fail regardless of free RAM*.
**To test, cheapest first:**
1. Raise the pagefile (raises commit limit; safe — reserved commit won't thrash). Immediate.
2. A/B **drop `--no-mmap`** on the big models (weights mmap'd from GGUF = not commit). Biggest
   single commit reducer; trade is slower cold load (amortized when preloaded).
3. Single-card serving (MoE path) halves GPU-backed commit + no spill — already chosen.
4. Physical RAM upgrade is the expensive option (slots 4/4 full at 8 GB -> replace with 4x16 GB
   for 64 GB); not strictly needed since physical RAM isn't the bottleneck.

## 3d. b70tools verdict gate: watch COMMIT headroom, not just free RAM (code)
The gate's host-RAM safety floor watches *free physical RAM*, which read totally safe (13.5 GB)
while commit was at the 92% edge — the actual allocation-failure risk. **Add a commit-headroom
floor to the verdict gate** (host.commit.available_bytes) so it catches the real ceiling. Pairs
with [[b70tools-verdict-gate]]. Small, high-value collector/gate change.

## 4. COOPMAT toggle — RESOLVED 2026-06-18: no-op
A/B'd via `eval/scripts/bench-config.ps1` on the new driver (`8826`), Qwen2.5-32B-Q4, 25k ctx:
`GGML_VK_DISABLE_COOPMAT` 1 vs 0 gave **identical** prefill (243 vs 241) and decode (4.17 vs
4.17). Toggle is effectively inert on this build/driver (no coopmat probe line in the startup
log either). Closed — the long-context decode bottleneck is the Vulkan attention kernel, not a
missing matrix path (see #8 and the 2026-06-18 retro).

## 5. Cache work — the real fixes (code, not just runs)
- **Tempo `ParseResult` disk cache** keyed by source SHA (`Sha256 = null // deferred` today).
  Turns repeated 600 MB+ parses into loads. The script-level evidence cache exists; this is the engine fix.
- **Prefix/KV cache hit-rate probe** in the inference path: send the same evidence prefix twice,
  assert 2nd-send prompt-eval collapses. Matters *more* on a contended box (warm prefix skips the
  contention-sensitive prefill entirely).

## 6. Already logged, deliberately deferred (not for a quick pass)
- **`trace.json` / `walkSeconds` nondeterminism** — leopard `PipelineTrace.cs:155` bakes wall-clock
  into the cached artifact. Extraction is an implementation-design call to make *with* the
  trace/Pipeline-Explorer work, not a drive-by. (See plan doc.)
- **Tempo `--percolation` / `--reaction-rate` diagnostics CLI mode** — would let the sweep exercise
  the Tempo percolation maths on real logs directly (today only xUnit fixtures reach them; leopard's
  `signals.v1` covers the equivalent shape end-to-end).

## 7. Post-sweep wrap-up (once all 7 models finish)
- Full per-model throughput table tonight vs 20260605 (label it "busy-box" — see golden rule).
- Confirm the MoE decode tiebreaker: did MoE hold decode better than mistral under the same host
  load? (Supports the bandwidth/contention story + the MoE-for-tooling pick.)

---

## 8. SYCL is the long-context decode path — 3.5x over Vulkan (2026-06-18)  ⭐⭐ highest value
**Found:** long-context decode on Vulkan is **attention-kernel-bound**, not config/memory.
Ruled out via `eval/scripts/bench-config.ps1` (Qwen2.5-32B-Q4, 25k ctx): coopmat, single-vs-dual
card, q8-vs-f16 KV, host contention — all left decode pinned at ~4.2 t/s; a live PDH probe
disproved the shared-memory-spill theory (clean card: 22.78 GB dedicated / 0.49 shared, no spill,
still 4.2). Tiny-ctx decode was 21.5 t/s — a 5x collapse purely from context depth = the
attention kernel. **Then:** the IPEX-LLM SYCL stack already on the box
(`D:\work\battlemage\intel_ollama\tools\ipex-llm-ollama\`, `ipex-llm-2.3.0b20250630`) ran the
SAME model/quant/prompt at **14.47 t/s decode (3.5x), confirmed n=4 <1% variance, coherent
output.** Cold load is 12x slower (131s vs 11s) — irrelevant for hot/`keep_alive` models.
**Decision rule:** long context (Hermes-125k, coding+repo) -> SYCL; short context / fast
cold-load -> Vulkan. Full write-up in the 2026-06-18 retro.

### Follow-ups (ordered)
- **8a (⭐):** SYCL decode at the REAL depth — load Hermes (or any 125k-capable model) under
  IPEX-LLM and measure decode at 125k. The 25k result is a proxy until measured on the model.
- **8b:** dual-card SYCL — can ipex-llm-ollama tensor-split across both B70s, and does it help or
  just cost? (tonight was single-card; Vulkan layer-split cost prefill, not decode.)
- **8c:** planner/critic two-lane, measured — pin a 2nd SYCL model to `ONEAPI_DEVICE_SELECTOR=
  level_zero:1` and confirm each lane holds ~14.5 t/s under simultaneous load. The operator's
  pattern, end-to-end. Model+KV fits one 32 GB card, so two independent lanes is the rig optimum.
- **8d (GATED — hazard):** does SYCL/Level-Zero overflow into host RAM, or hard-stop at VRAM?
  Determines the real per-card model-size ceiling. **Run ONLY with the verdict host-floor
  guardrail** — deliberate max-VRAM is the documented cascade->non-POST hazard. ollama plans
  conservatively (partial-offloads rather than OOM), which keeps it on the safe side; read its
  offload-decision log (memory.available / required / layers.offload) at rising context.
- **8e (b70tools code):** SYCL/Level-Zero VRAM is ~invisible to the PDH `GPU Adapter Memory\
  Dedicated Usage` counter (showed 1 GB while ollama held 29 GB; same counter read Vulkan at
  22.8 GB). A new cross-observer disagreement class — add a `source_blind_to_allocation` style
  rule so the verdict gate is never trusted on a SYCL workload. Pairs with [[cross-process-vram-signal]].

### Capacity note (operator-corrected 2026-06-18)
Each B70 is **32 GB physical VRAM, period** — 64 GB pooled across the two for fast work. The
Task-Manager "48 GB per card" is 32 GB VRAM + a ~16 GB window into the **32 GB host DDR4**
(PCIe-slow, shared across both cards + OS). Do NOT plan capacity off "48/card."
