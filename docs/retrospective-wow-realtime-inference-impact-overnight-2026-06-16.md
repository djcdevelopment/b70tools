# Retro: overnight regression harness + the post-driver baseline that wasn't

*One session built a three-repo regression harness, ran an unattended 11-hour 7-model GPU sweep,
and turned a scary-looking "driver regression" into a clean characterization of a contended box —
catching three hardware truths the operator's screenshots and domain knowledge fed in along the way.*
*Date: 2026-06-16 · Scope: this session (working tree on `research-wow-realtime-inference-impact`; no new commits until this retro)*

---

## What shipped

The operator updated GPU drivers, generated a fresh combat-log corpus, and ported two layers of
"maths" out of JavaScript — and wanted regression tests theorized and an overnight run staged on
the dual B70s. The session delivered a runnable harness and an investigation, not just a plan.

**Harness (all new, `scripts/overnight-regression/` + `scripts/tooling/`):**
- `Start-ParserRegressionSweep.ps1` — builds Tempo, runs all three regression suites (Tempo
  Parity+Percolation, leopard-host 62 xUnit, leopard-web vitest), then sweeps 17 combat logs
  through `Tempo.Diagnostics --memory-bench` / `--export-evidence` with a SHA-keyed evidence cache
  and a determinism double-export.
- `Invoke-LeopardArtifactSweep.ps1` — drives `leopard-host --headless` to derive all 12 per-night
  JSON artifacts per log, with determinism + cache-skip + schema regression checks.
- `Run-OvernightAll.ps1` — the walk-away orchestrator (Phase 1A/B → 1C → Phase 2 stress sweep).
- `Start-AlwaysHotMoE.ps1` — a single-card always-hot MoE supervisor (OpenAI endpoint on :8090)
  over the existing `Start-SecondB70LlamaServer.ps1`.

**Docs:** `docs/overnight-regression-plan-2026-06-16.md` (plan + live-updated findings),
`docs/inference-test-backlog.md` (queued follow-ups), this retro, plus refreshes to the README's
rig-issues table and the parent `wow-realtime-inference-impact-plan.md`.

**The run:** completed 15:52:37, all phases exit 0, ~11 h (Phase 2 dominated), zero crashes/TDR.
Phase 1 (CPU) is valid and green; Phase 2 (GPU throughput) is real but contaminated by concurrent
host load and is explicitly NOT a usable post-driver baseline.

The maths in question: **Tempo** `Core/Analysis/Percolation/` (union-find largest-cluster
"critical ratio" + a move→stabilize reaction-rate hysteresis), and **leopard-host** (the RaidUI
reducer corpus ported into 9 ShapeArtifact modules with a per-night JSON disk cache, ADR-0005 over
in that repo). b70tools is the experiment owner; both others are read-only suppliers.

---

## Engineering Lead perspective

The harness was built test-first against reality, not against assumptions. Every script was
parse-checked on PowerShell 5.1, scrubbed for non-ASCII (an em-dash in a `StringBuilder.AppendLine`
broke the 5.1 parser early — a one-character lesson re-learned), dry-run for corpus discovery, and
the load-bearing pieces were smoke-tested before the overnight run committed 11 hours to them. The
`--memory-bench` stdout regex was validated against the real exe; the leopard headless path was
proven end-to-end on the 0.8 MB stub log (health in 0.5 s, `/api/parse` ok, cache-path formula
matching `Program.cs` exactly). That up-front de-risking is why the overnight run produced clean
exit codes instead of a 2 a.m. silent failure.

Two real bugs surfaced — both in the harness, both caught by *inspecting results rather than
trusting the summary line*. First, `Get-ChildItem -Recurse | Select -First 1` grabbed a stale
`bin\Release\net9.0\leopard-host.exe` (built 2026-06-03, before the math-port marathon) that only
writes the boxscore `.md`; the real host is `net9.0-windows\`. That single wrong path manufactured a
fake "determinism DIVERGED on all 11 artifacts" finding. Fixed to select by TFM + newest, and the
determinism test was rewritten to be **twice-fresh** (wipe→derive ×2) so stale on-disk artifacts
from an older build can never confound it again. Second, the "Flagged: 19" headline on a 17-log run
was PowerShell returning a single `[ordered]` row from `Where-Object` and reporting its *key count*
via `.Count` — fixed with `@(...)`. Neither was a product bug; both were harness bugs that would
have shipped a false conclusion if not chased.

The most satisfying engineering moment was watching b70tools do its actual job. When the 70B split
spilled ~6 GB into shared host memory, b70tools' PDH per-adapter `non_local` committed counter read
6.23 GB on the distressed card — matching Task Manager's 5.8 GB shared, and on the *exact* card whose
IGCL telemetry historically goes silent. The cross-process VRAM signal the whole tool exists for
caught a red-line condition a per-process Vulkan budget never could. The verdict-gate refinement
(watch `host.commit.available_bytes`, not free RAM) is the one piece of code debt this stretch
*added* — small, well-scoped, and now in the backlog (#3d).

---

## Project / Program Manager perspective

Scope landed faster than the framing implied. The opening ask was "theorize regression tests and
propose an overnight run." It became a built, validated, executed harness across three repos plus a
full investigation — in one session. The early estimate ("Phase 1 is multi-hour") was wrong by an
order of magnitude (it ran in ~130 s total); the real long pole was always Phase 2's GPU sweep
(~11 h), and naming that correctly up front would have set expectations better.

The headline deliverable — a clean post-driver throughput baseline — was **not** achieved, and
that's the honest status. The box was deliberately shared (other apps, other agents), which clamped
every model to a uniform ~200 prefill / ~5.7 decode ceiling and made the throughput numbers
unusable for A/B. That's not a failure of the harness; it's a scoping reality the operator chose
knowingly (the tests no longer need a BIOS flash, so the strictness budget relaxed). The clean-room
re-run is now backlog item #2, promoted from optional to required.

What the run *did* retire: three hardware uncertainties. The "did the driver update regress
performance" risk is closed (it didn't — contention did). The "is dual-card 70B survivable near the
VRAM ceiling" risk is closed (yes — 11 h, zero TDR, even while spilling). The "what's the real host
memory limit" question is answered (commit charge, not physical RAM). New dependency surfaced: the
clean-room re-run depends on a quiesced box, which is an operator-scheduling constraint, not a code
one. Everything deferred is deferred with a written reason in the backlog — nothing is "pending,
unknown."

---

## QA / Verification perspective

The verification pattern that earned its keep: **don't trust the summary, read the artifacts.**
Both harness bugs hid behind green-looking or scary-looking summary lines and only fell out when the
underlying manifests/cache files were inspected directly. The "19 flagged" tally and the
"DIVERGED on all 11" determinism result were both artifacts of measurement, not the system under
test — and the discipline of asking "is this physically plausible?" caught both. A uniform
~5.7 tok/s decode across a 3B-active MoE and a dense 32B is *not* plausible for real inference; that
implausibility is what flipped the read from "regression" to "contamination."

Cross-checking with independent evidence was the through-line. The prefill-drop hypothesis was
tested against config parity (identical flags, identical `prompt_n`), then against GPU telemetry
(clocks higher, temps normal — no throttle), then against host telemetry (RAM 18%→38-54%), then
against the full 6-model pattern (uniform clamp). Each layer narrowed the attribution; the final
call (host contention, not driver) rests on four independent signals agreeing, not one. The commit-
ceiling finding was corroborated twice — Task Manager screenshot *and* b70tools telemetry both put
commit at 92% with physical RAM at 60%.

What's verified vs not: Phase 1 (parser correctness, both math layers' suites, leopard artifact
determinism for 11/12, cache-skip) is genuinely verified and trustworthy — those are pass/fail,
immune to the throughput contention. Phase 2 throughput is *measured but invalid* as a baseline and
labeled as such everywhere it appears. The one uncovered item carried forward honestly:
`trace.json` is intermittently nondeterministic (`PipelineTrace.cs:155` bakes `walkSeconds` wall-
clock into the cached artifact) — root-caused, logged, deliberately not fixed.

---

## Operator perspective

*(first person — Derek)*

I came in with three loose threads — new drivers, new combat logs, maths I'd moved out of JS — and
wanted them turned into something I could run while I slept. The bones of these tests have gotten
safe enough now that I don't need to flash the BIOS to recover from a bad dual-card start, and that
changed how I treat the rig: I stopped babysitting it. I left other programs open, let other agents
finish work in the background. That was a deliberate call, and it's exactly what muddied the
throughput numbers — but I'd make the same call again, because the *correctness* runs don't care and
the throughput baseline is cheap to re-get on a quiet night.

The moment I had to step in was the driver-regression call. The numbers looked like a clean −46%
prefill hit and the analysis was confidently pinning it on the driver — but I knew the box was busy,
so I said so: probably IO or PCIe lane bound, not the driver. That's the kind of call the AI couldn't
make because it didn't know what *I'd* been running on the machine. Same with the things I had to
physically look at and feed in — the x8/x8 bifurcation, the shared-GPU-memory bar finally lighting
up on the 70B, the commit charge sitting at 92% while RAM looked fine. Those are screenshots and
hardware facts I had to surface; the agent then ran them down with telemetry and turned each into a
real finding.

I also made a couple of "not now" calls I want on the record. The `trace.json` nondeterminism is a
real bug but pulling that timing value out of the cached artifact is a design decision that belongs
*with* the trace-surface work, not as a drive-by — so it stays a logged finding. And I let the 70B
finish even though its throughput was already junk, because "does the 39 GB dual-card model run
11 hours without a TDR while spilling into host memory" is itself the answer I wanted. The strategic
intent underneath all of this: figure out whether one of these B70s can quietly be my local-tooling
brain to keep simple work off the cloud. The answer that's forming — MoE, single card, x16, always
hot — feels right.

---

## How we worked together (human ↔ AI)

### What worked well

- **The operator's domain knowledge caught a premature attribution the AI was about to ship.** When
  the analysis declared "the only variable is the driver update," Derek's "I was being less strict,
  other agents were running, possibly IO/PCIe bound" was the correction that mattered — and the AI
  then *verified* it with telemetry (GPU clocks higher, host RAM 2-3x) rather than just deferring.
  Human supplies the ground truth the machine can't observe; machine runs it down. That's the loop.
- **Screenshots as a sensor the AI doesn't have.** Three of the night's best findings — PCIe
  bifurcation, shared-memory spillover, commit-at-92% — came from the operator looking at Task
  Manager and feeding it in. Each became a telemetry-corroborated finding within one exchange.
- **De-risking before the long commit.** Validating every script (parse, dry-run, smoke test,
  regex-against-real-output) before launching an 11-hour run meant the overnight produced clean exit
  codes, not a silent 2 a.m. death. The smoke test on the 0.8 MB stub validated the entire leopard
  headless pipeline cheaply.
- **"Read the artifacts, not the summary" as a shared reflex.** Both harness bugs were caught by
  distrusting a summary line and opening the underlying files. The AI flagged its own "MoE is done"
  error (dir exists ≠ run complete) the same way.
- **Deferral with written rationale.** "Keep it as a logged finding, extraction belongs with
  implementation considerations, not now" got captured verbatim in the doc so it can't later be
  mistaken for an open one-liner.

### What didn't

- **The AI overclaimed twice before the operator/data reined it in.** First "driver did it" (host
  wasn't controlled), then even "host RAM contention" was under-stated once the full sweep showed a
  hard uniform clamp (heavier concurrent GPU time-sharing, not just RAM pressure). The lesson isn't
  "be vaguer" — it's "state the confound *with* the finding," which the corrected docs now do.
- **Confident wrong intermediate calls.** "MoE finished" (it was still on round 2 — run dirs are
  created at model *start*) and "Flagged: 19" (a `.Count` gotcha) both shipped to the operator before
  being walked back. Cheap to correct here, but each was a moment of false confidence.
- **Windows text-encoding papercuts, twice.** An em-dash broke PS 5.1 script parsing; a `Δ` broke
  Python's cp1252 stdout. Both are known hazards on this account and both still slipped through once.
- **Early ETA was an order of magnitude off** ("Phase 1 multi-hour" → 130 s), which would have
  mis-set expectations if the operator had been waiting on it.

### Patterns to repeat

- Validate-then-commit for any long unattended run (parse + dry-run + smoke + real-output regex).
- Treat operator screenshots/hardware observations as first-class sensors; corroborate each with the
  tool's own telemetry.
- When a result looks dramatic, ask "is this physically plausible?" before reporting it as a finding.
- Capture deferral decisions and their rationale in-doc, in the operator's words.

### Patterns to change

- **Lead with the confound.** Any performance delta on an uncontrolled box gets "(host not
  quiesced — needs clean-room A/B)" attached at first mention, not after pushback.
- **"Artifact exists" is never "work complete."** Gate completion claims on the terminal artifact
  (manifest written), not the directory.
- Run a non-ASCII scan as a reflex on every generated Windows script before first execution.

---

## Lessons learned

1. **A dramatic measurement is a hypothesis, not a finding, until the environment is controlled.**
   The −46% "driver regression" was real data and a wrong conclusion. Identical config rules out
   config; it does not rule out the host.
2. **Physical implausibility is a fast contamination detector.** A 3B-active MoE and a dense 32B
   cannot decode at the same speed. When unrelated systems converge to one number, suspect a shared
   external limiter, not the systems.
3. **On a GPU box, the host memory wall is commit charge, not free RAM.** WDDM backs VRAM with
   system commit; `--no-mmap` piles on. Free RAM can look safe while you're one allocation from
   failure. Gate on commit headroom.
4. **The cross-process signal is the whole point.** b70tools caught the shared-memory spillover that
   every per-process budget API is blind to — exactly the gap that justified building it.
5. **Single-card x16 beats dual-card x8/x8-plus-split for anything that fits one card.** The
   bifurcation and the inter-card transfer are a structural tax the small/mid models never need to pay.

---

## Next moves

All queued in [`docs/inference-test-backlog.md`](docs/inference-test-backlog.md):

- **#1 / #2 (⭐):** single-card x16 vs dual-card x8/x8 MoE A/B, and the clean-room contention-free
  baseline — both need a quiesced box; both now the highest-value next runs.
- **#3b:** retune the 70B tensor-split (or drop `-fit off`) to stop the shared-memory spill; success
  = `non_local == 0`.
- **#3c / #3d:** commit-ceiling mitigations (pagefile, drop `--no-mmap`, single-card), and the
  b70tools verdict-gate change to watch commit headroom instead of free RAM.
- **#3 (use case):** stand up the always-hot single-card MoE (`Start-AlwaysHotMoE.ps1`, already
  built) and point a real MCP/tool at `:8090/v1`.
- **#5 (code):** the Tempo `ParseResult` disk cache and the prefix/KV-cache hit-rate probe.
- **Logged + deferred:** `trace.json`/`walkSeconds` extraction (with the trace-surface work, not
  before); a Tempo `--percolation` diagnostics CLI mode.

---

## Acceptance gates met

- [x] Regression tests theorized for the new maths + new combat-log corpus
- [x] Caching-to-disk lessons surfaced and tested (leopard per-night JSON cache: determinism +
      cache-skip; SHA-keyed evidence cache)
- [x] Overnight dual-B70 run staged and executed unattended (3 phases, exit 0, ~11 h)
- [x] Parser + both math layers verified green on the new post-patch corpus (3 suites pass; 11/12
      leopard artifacts deterministic)
- [x] Dual-card 70B stability characterized (zero TDR/crash over 11 h, even while spilling)
- [x] MoE selected + built as the single-card local-tooling backend
- [~] Post-driver throughput baseline — **measured but invalid** (host contention); clean-room
      re-run required (backlog #2)
- [ ] `trace.json` determinism — deferred by decision (extraction belongs with the trace surface)
- [ ] Clean-room A/B (single-vs-dual, contention-free baseline) — scheduled for a quiesced night
