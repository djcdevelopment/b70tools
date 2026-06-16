# Overnight regression + dual-B70 plan — 2026-06-16

**Owner:** b70tools (experiment owner). **Workload supplier:** Tempo (`D:\World of Warcraft\Tempo`).
**Trigger:** new combat-log corpus generated post-driver-update, plus the percolation/
reaction-rate "maths" ported JS → C# into the Tempo parser/analysis pipeline.

This run establishes the **first post-driver-update baseline** for both parser perf and
inference throughput. Keep model/round/prompt counts identical to the `20260605` stress
runs so the inference comparison is apples-to-apples.

## Three repos, two math layers

The research line spans three repos. Both math layers were ported out of JS and need
regression coverage:

- **Tempo** (`D:\World of Warcraft\Tempo`, C#) — the **parser engine** plus the
  **percolation/reaction maths** in `Tempo.Core/Analysis/Percolation/`. Tests: the JS↔C#
  golden `Parity/` tree + Percolation/ReactionRate in `Tempo.Core.Tests`.
- **leopard-host** (`D:\work\leopard\src\leopard-host`, C#) — the **RaidUI math corpus**,
  ported as 9 ShapeArtifact modules (ADR-0005): `SignalsArtifact` (six-signal pack),
  `PullDiff`, `WipeClassifier`, `CoverageTimeline`, `MovementAffinity`, `FormationSegments`,
  `PlayerScores`, `ParticipantMeters`, `PullDivergence`. Pattern = **parse-time compute →
  per-night JSON cache → thin API**. Tests: 62 xUnit, oracle = RaidUI `__tests__`.
- **leopard-web** (`D:\work\leopard\src\leopard-web`, JS) — the CanonicalContext/lens
  composer. Tests: vitest (`npm test`), 18.

### The two math layers

**Tempo / `Percolation/`:**
- `PercolationAnalyzer` — proximity graph over positioned players (X/Y + mapId, edge if
  within `edgeThresholdYards`), weighted union-find, `criticalRatio =
  largestConnectedComponent / N`. **High ratio = stacked, low = spread.** O(N²) per snapshot.
- `ReactionRateAnalyzer` — hysteresis state machine over the percolation series (drop <
  `0.60`, stabilize > `0.90`, hold frames), emits move→stabilize reaction latency in ms.
  Plus `AnalyzeMechanicAnchored`.

**leopard-host artifacts (the disk-cache layer — this is the caching-to-disk you meant):**
`POST /api/parse {names:[...]}` runs **one `ParserPipeline.Parse` → 12 cached artifacts**
under `%LOCALAPPDATA%\Leopard\cache\{name}__{mtimeTicks}.{kind}`: `boxscore(.md)`,
`night.v1`, `trends.v2`, `trace`, `career`, `shape.v1`, `signals.v1`, `affinity.v1`,
`players.v1`, `coverage.v1`, `segments.v1`, `classify.v1`. Cache key = filename + mtime
ticks; the whole set re-derives if **any** member is missing. `--headless` serves the API
on `:5280` with no desktop shell ("for cache backfills, scripting, and tests").

### Corpus + parser cost

- **New combat-log corpus** (since round 1): ~15 logs 19 MB–688 MB plus canon fixtures
  (`fixture-canon/.../2026-05-27-m+-mixed-keys` 227 MB, `liveplay-2026-05-26` 52 MB). The
  600 MB+ logs (06-05, 06-06, 06-12) are likely raid nights — the data that finally lets
  named-offender attribution fire on real raid-night input (a 4-retro-old uncovered gap).
- **Parser is eager / full-file** — `ParserPipeline.Parse` materializes the whole
  `ParseResult`; Tempo has no disk cache (`Sha256 = null // deferred`). leopard-host adds
  the per-night JSON cache on top. Memory tracked via `--memory-bench` / INV-PX1.5 /
  ADR-018 2.2 GB budget.

## Regression-test theory

1. **Post-patch format drift (cheapest to break).** `COMBAT_LOG_VERSION` header gate in
   `ParserPipeline`. A parse that returns zero encounters / zero pulls on a known-good log
   is the format-drift tripwire. Position coverage matters: percolation is dead without
   X/Y from advanced logging — a session logged without advanced logging silently yields
   garbage `criticalRatio` rather than an error.
2. **JS→C# parity + determinism.** The whole `Tempo.Core.Tests/Parity/` tree already
   guards Lex/Classify/Segment/Reconcile/Replay/AdvancedFields parity against golden JS
   output. Re-run it against the current `main`. Specific port-bug risk in the new maths:
   `PercolationAnalyzer` picks the largest component with `componentSize.MaxBy(kv =>
   kv.Value)` — **ties break on Dictionary iteration order**, which is not guaranteed and
   won't match JS object key order; the chosen root flows into the `involved` participant
   list. Evidence-export run twice on the same log must be byte-identical (this is also the
   llama-server prefix-cache prerequisite — see below).
3. **Maths semantic sanity (stack vs spread).** The reaction model assumes "spread then
   re-stack." For fights you are *supposed* to spread on, the semantics invert. Covered by
   `PercolationAnalyzerTests` / `ReactionRateAnalyzerTests`; the overnight sweep adds the
   real-log smoke (do the analyzers produce sane event counts on the new corpus).
4. **Memory/perf budget with the heavy maths.** `--memory-bench` on the 688 MB log must
   keep the `ParseResult` heap delta under the 2.2 GB budget (branch (a)/(b), not (c)),
   and parse wall-time must not regress vs baseline.

## Caching-to-disk + optimization lessons → tests

- **The per-night JSON artifact cache (leopard-host) is the primary disk-cache surface.**
  Lessons from the math-port retro + disk-confirm pass:
  - **Determinism**: clear the cache, re-derive, and the 12 artifacts must be byte-identical
    run-to-run. Nondeterministic ordering (dictionary iteration, unstable sorts) silently
    poisons both the artifact diff *and* any downstream prompt prefix-cache.
  - **mtime-keyed invalidation**: the cache key is `{name}__{mtimeTicks}`. A re-`POST
    /api/parse` with the set present must **not** re-parse (cheap, no rewrite); a missing
    member re-derives the whole set. Test both paths.
  - **Schema conformance**: the disk-confirm found real drift — uninventoried pull fields
    (`ageDays`, `close`), a nested `density.maxBucket`, lowercase `outcome`, and on-disk key
    names that differ from inventory IDs (`pullId`, `endHpPct`). Assert the known keys exist
    in each artifact; that class of silent mismatch bites the lens builders.
- **Re-parsing a 688 MB log every iteration is the waste.** Tempo has no `ParseResult` disk
  cache; the Tempo-side sweep caches the **evidence-markdown keyed by source SHA-256**
  (`runs/_evidence-cache/<sha>.md`) so re-runs are loads. The real fix (cache the
  materialized `ParseResult`) is a Tempo C# follow-up.
- **llama-server prefix/KV cache is the inference lever.** `EvidenceExporter.Build` emits a
  canonical, stable-ordered markdown prefix for "prefix-cache workflows against a local LLM"
  (`LlamaServerSlotCache.cs`). Test: send the same prefix twice, confirm 2nd-send prompt-eval
  collapses. **If evidence ordering is ever nondeterministic, the prefix cache silently
  misses** — the determinism test and the cache-hit test are the same bug surface.
- **jsonl flush** — already fixed (`cb258b8`, per-tick flush). The live loop's
  `live-insight.jsonl` (ADR-0006) is both the overlay feed and the critic-loop eval corpus;
  every insight is replayable (evidence + prompt + params + output joined by `insightId`).
- **The 0.24 GB free-RAM cliff from round 1**: preload llama-server, never cold-load GGUF
  mid-run, gate on a RAM floor.

## Tonight's run order (walk-away)

**Preflight (gate, ~1 min):** `b70tools verdict` for host-RAM floor + per-card VRAM + IGCL
advisory. Snapshot driver version into the manifest. Bind the inference card by **stable
PCI-BDF, never vk:N** (it drifts between sessions).

All of Phase 1 is CPU-only and zero-GPU-risk — it runs first, unattended, and cannot harm
WoW or the cards. Phase 2 is the GPU inference sweep.

**Phase 1A — test suites (the regression reruns):**
`scripts/overnight-regression/Start-ParserRegressionSweep.ps1` runs all three:
- Tempo: `dotnet test src/Tempo.sln` (Parity + Percolation + ReactionRate).
- leopard-host: `dotnet test src/leopard-host` (62 RaidUI-parity xUnit tests).
- leopard-web: `npm test` (vitest, 18).

**Phase 1B — Tempo parser perf sweep (engine cost + Tempo-side maths):**
For each new log + canon fixture: `--memory-bench` (parse wall, heap delta, % budget,
session/encounter/pull counts, branch verdict) and `--export-evidence` (LLM artifact;
SHA-keyed cache; determinism double-export on a representative log). Assertions: parse exit
0; encounters > 0 (or flagged M+-only); heap delta under 2.2 GB.

**Phase 1C — leopard artifact + cache sweep (the RaidUI maths on real logs):**
`scripts/overnight-regression/Invoke-LeopardArtifactSweep.ps1` — build leopard-host, launch
`--headless` on `:5280`, `POST /api/parse` each new log (one parse → 12 artifacts), then:
- assert all 12 artifact files land per log (the 9 ported math modules ran clean on real
  post-patch data);
- capture derive wall-time + artifact sizes per log;
- **determinism**: delete one log's cache, re-derive, diff the artifacts byte-for-byte;
- **cache-skip**: re-`POST` and confirm no rewrite (mtime hit);
- **schema**: assert known keys exist in `signals.v1` / `players.v1` / `classify.v1`.

Output of Phase 1: `results.json` (no BOM) + `summary.md`, the cached evidence prefixes, and
the populated `%LOCALAPPDATA%\Leopard\cache` — all feeding Phase 2.

**Phase 2 — inference stress sweep (single-card, preloaded):**
`scripts/stress-suite/Start-StressSuite.ps1` — small→large model order, **preloaded
llama-server** (not cold llama-cli), host-pressure sidecar. Changes from the `20260605` runs:
(1) point `-SnapshotPath` at a Phase-1B evidence prefix so we stress realistic coaching
prompts, (2) add a prefix-cache probe (same evidence twice, assert prompt-eval collapse). The
leopard live loop targets llama-server `:8080` on the 2nd B70 — keep that the bound card.

**Safety rails:** one active request at a time, drop stale; free-RAM watchdog aborts the
current model under ~3 GB; no dual-card layer split unattended; everything to a timestamped
run dir with driver version + BDF binding + Tempo commit + b70tools commit in `manifest.json`.

## Morning pass/fail

- All three suites green: Tempo (Parity + Percolation/ReactionRate), leopard-host (62),
  leopard-web vitest (18).
- Tempo parses all new logs (exit 0, non-zero encounters where expected).
- Heap delta under 2.2 GB even on the 688 MB log → branch (a)/(b), not (c).
- Evidence export byte-identical across two runs (prefix-cache-safe).
- leopard: all 12 artifacts derive per log; re-derive is byte-identical (cache-deterministic);
  re-`POST` hits the cache without rewrite; known schema keys present.
- Inference sweep completes with no RAM-floor abort / device-lost.
- Per-model tokens/sec vs the `20260605` baseline → did the driver update move throughput.

## FINDING (full sweep): tonight's GPU throughput is CONTAMINATED — concurrent-workload clamp

The full 6-model sweep makes it unambiguous and corrects the partial read below. **Every model —
config-verified identical to baseline — converged to a uniform ~200 tok/s prefill and ~5.7 tok/s
decode ceiling, regardless of size/quant/architecture.** Decisive tell: MoE, which decodes ~11.5
tok/s clean (and should be ~2x a dense 32B), was pinned at ~5.7 — the same as the 32B-Q6. A
uniform throughput clamp independent of the model is not physical for genuine model-bound
inference; it is the signature of **another workload time-sharing the cards** (GPU clocks were
*high* and temps normal, so the cards were busy — just not all for us). Matches the operator's
note that other agents/programs were running.

Corrections to the partial read: the earlier "32B got faster tonight (+49%)" was a baseline-data
artifact (that baseline manifest recorded all-zero decode timings) — disregard it. The valid,
config-matched deltas are 14b decode flat, mistral −26%, **MoE 11.5 → 5.7 (−51%)**.

**Consequence: tonight's Phase-2 throughput numbers are NOT a usable post-driver baseline.** They
measure a contended box. The clean-room re-run (backlog item #2 in
`docs/inference-test-backlog.md`) is now **required**, not optional, for real numbers. Phase-1
results (parser/maths/determinism — pass/fail, not throughput) are unaffected and remain valid.
The driver still looks fine; nothing here implicates it.

### (superseded partial read — kept for the telemetry evidence)
Earlier 2-model read called it host RAM contention (prefill −46%, host RAM 18% -> 38–54%, GPU
clocks higher, temps unchanged). Directionally right, understated: the full sweep shows a hard
uniform clamp, i.e. heavier concurrent GPU/host load than "RAM pressure" implies.

First completed Phase-2 model (qwen2.5-14b, dual-card). llama config is provably identical —
same ctk/ctv (q8_0), fa=on, sm=layer, ts=1,1, fit=off, ngl=99, ctx=32768, max_tokens=2000,
same 70k snapshot, same env, same llama-server binary (2026-05-24, predates both runs),
`cache_n=0` (cold prefill) both, identical `prompt_n` per prompt.

| Metric | 20260605 | tonight | Δ |
|---|--:|--:|--:|
| Prompt-eval / prefill | 386 tok/s | 210 tok/s | −46% |
| Generation / decode | 5.68 tok/s | 5.64 tok/s | flat |

**ATTRIBUTED (telemetry-backed): host-side contention, NOT the driver and NOT thermal.**
Two more completed models + tonight's b70tools telemetry settle it:

- Prefill dropped **uniformly −46%** (14b 371→200, mistral 374→202); decode held flat on 14b
  (−1%) but fell −26% on the larger mistral. A driver prefill-kernel change can't explain the
  size-dependent decode hit.
- GPU telemetry tonight vs baseline (mistral run, good card): freq **2653 MHz mean (higher
  than baseline's 2588)**, temp 74C/79max (same as baseline 71/79). **No clock or thermal
  throttle — the cards were healthy.**
- The one thing that changed: **host RAM used 38% mean / 54% max tonight vs 18%/19% baseline**
  — the background apps + other agents the operator noted (the box is run less strictly now
  that tests don't need a BIOS flash). Prefill is the host-sensitive phase (streams the 70k
  prompt through CPU/RAM/PCIe); decode is on-GPU. That's exactly the observed pattern.

Conclusion: the −46% is **host contention**, not a driver regression. The driver update looks
fine. Lesson: **quiesce the box for any benchmark meant to be comparable.** A clean-room A/B is
still worth one pass to put a number on the contention-free baseline, but it is no longer a
suspected-driver investigation.

Practical takeaway, now stronger: on a deliberately-shared box, **avoid the contention-
sensitive prefill** — the llama-server prefix/KV cache (warm prefix -> skip prefill) and a disk
parse cache are exactly the right mitigations. MoE (model 3, still running) is the tiebreaker:
if its decode holds up better than mistral's under the same host load, that further confirms
the bandwidth/contention story and reinforces MoE as the local-tooling pick.

## First-run results (2026-06-16 ~04:20, shakedown before the overnight run)

Phases 1A/1B/1C all executed; two harness bugs found and fixed, leopard maths verified green.

**Phase 1A/1B (parser) — GREEN.**
- All three suites pass: Tempo (26.6 s), leopard-host 62 xUnit (4.1 s), leopard-web vitest (2.1 s).
- 17 logs parsed clean; heap peaks at **20.6 % of the 2.2 GB budget** on the 627 MB log
  (BRANCH (a) — eager v1 stays comfortable). Only real flag: the 0.8 MB empty stub
  (`NO-ENCOUNTERS`, correct). The "Flagged: 19" headline was a PowerShell `.Count`-on-a-single-
  `[ordered]`-row gotcha (reported its 19 keys); fixed with `@(...)`.

**Phase 1C (leopard) — GREEN after fixing a harness bug.**
- First run reported "determinism DIVERGED on all 11 + cache-skip miss." Root cause was **my
  harness, not leopard**: `Get-ChildItem -Recurse | Select -First 1` grabbed a **stale
  `bin\Release\net9.0\leopard-host.exe` (built 2026-06-03, pre-math-port)** that only writes
  the boxscore `.md`. The real host is `net9.0-windows\` (csproj TFM). Fixed to select by TFM +
  newest. Determinism test also rewritten to **twice-fresh** (wipe→derive ×2) so stale
  on-disk artifacts can't confound it again.
- Re-run (8 raid nights): **all 12 artifacts derive per log**, including the 600 MB+ nights
  (628 MB → 16.8 s, 629 MB → 13.2 s). Twice-fresh determinism: **11/12 byte-identical**;
  cache-skip hits (no rewrite).
- **One real finding (LOGGED, deliberately deferred — not a pending one-liner):**
  `trace.json` is nondeterministic — `PipelineTrace.cs:155` embeds `walkSeconds =
  Math.Round(sw.Elapsed.TotalSeconds, 1)` (wall-clock parse time). It's a diagnostic artifact
  (Pipeline Explorer), not LLM evidence, so it cannot poison a prompt prefix-cache — low
  stakes today. **Decision (2026-06-16): leave leopard untouched.** Extracting `walkSeconds`
  from the cached artifact is a small refactor, but where the timing goes (sidecar? response
  field? dropped?) is an implementation-design call that should be made *with* the rest of
  the trace/Pipeline-Explorer work, not as an isolated drive-by change. Revisit when that
  surface is being worked, not before.
- Minor: nights parsed before a surface existed show that artifact missing until re-parsed
  (e.g. `night.v1.json` on older logs); a re-`POST /api/parse` backfills it (Program.cs
  re-derives the whole set when any member is absent).

## Notes / known gaps

- **The Tempo percolation/reaction maths are still not reachable from `Tempo.Diagnostics`** —
  only via xUnit fixtures. The leopard `signals.v1` artifact *does* surface
  spacing/followership/entropy per second on real logs, so Phase 1C exercises the equivalent
  shape end-to-end; a Tempo `--percolation` diagnostics mode is still the cleaner direct test.
- **`buildCareerLens` vitest test still does not exist** (4 retros old, a pure fully-specified
  function) — a standing leopard-web coverage gap worth closing in this pass.
- **named-offender attribution has never fired on real raid-night data** — the 600 MB+ raid
  logs in this corpus are the first chance to exercise it; check the `coverage.v1` /
  `classify.v1` artifacts on those nights for a real named offender.
</content>
</invoke>
