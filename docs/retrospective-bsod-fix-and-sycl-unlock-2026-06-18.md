# Retro: the driver fix that held, and the 3.5x that was sitting in a folder

*A session that started with "I found another way to BSOD the box" and ended with a
verified driver fix, a fully root-caused slow-decode mystery, and a 3.5x long-context
inference unlock discovered in a build the operator had written off in May.*
*Date: 2026-06-18 - Scope: this session (working tree on `research-wow-realtime-inference-impact`; no new commits until this retro)*

---

## What shipped

This was an investigation session, not a feature session. The single repo artifact is a new
micro-benchmark harness, `eval/scripts/bench-config.ps1`; the rest of the value is findings,
captured in auto-memory and in the doc updates that ride with this retro. Three arcs, each
feeding the next:

1. **Driver fix verified.** The operator found a *third* reproducible BSOD trigger
   (`Ctrl+Alt+Del` under inference load, after `Win+Shift+S`). Rather than just log it, we
   re-ran the exact original crash conditions on the freshly-reinstalled driver
   (`32.0.101.8826`, up from `8801`): overlay ON (`hud.exe` + composer), dual-card 32B load,
   then fired both triggers. **No crash** - just an audible 3-5 ms GPU stall on the
   secure-desktop switch where it used to `0xD1`. The `igfxnd` mode-set-under-load bug is
   fixed between `8801` and `8826`, the overlay is exonerated, and an independent reporter on
   the bug tracker confirms it was a real shared driver defect, not this rig.

2. **Slow-decode root-caused.** Long-context decode on the 32B (Qwen2.5-32B-Q4, ~25k-token
   prompt) sat at ~4.2 tok/s. Using the new harness we ruled out, one variable at a time:
   coopmat (no-op), single vs dual card (decode unchanged, prefill halved), KV q8 vs f16
   (no change), host contention (no change), and - decisively - a **live PDH probe** that
   disproved the shared-memory-spill hypothesis (the clean card held 22.78 GB dedicated /
   0.49 GB shared, *no spill*, and still decoded 4.2). Verdict: it's the **Vulkan
   attention kernel at long context**, not config, not memory placement. Tiny-context decode
   was 21.5 tok/s - a 5x collapse purely from context depth confirms the attention diagnosis.

3. **3.5x unlock found, already installed.** The fix for (2) was a different backend. The
   IPEX-LLM SYCL stack was already on the box at
   `D:\work\battlemage\intel_ollama\tools\ipex-llm-ollama\` (`ipex-llm-2.3.0b20250630`), a
   build the operator's own README had relegated to "not a perf candidate." Same model, same
   quant, same 25k prompt: **Vulkan 4.17 tok/s -> SYCL 14.47 tok/s, 3.5x, confirmed n=4 at
   <1% variance, output coherent.** The prior "SYCL loses" verdict was reached only at
   *short* context, where the gap is +10%; at the long context that is the operator's actual
   workload, SYCL's attention kernel does not collapse the way Vulkan's does.

A b70tools-relevant byproduct: SYCL/Level-Zero VRAM is nearly **invisible to the PDH
`GPU Adapter Memory` counter** (1 GB reported while ollama held 29 GB resident; the same
counter read Vulkan cleanly at 22.8 GB). That is a new cross-observer disagreement class -
exactly the kind of thing this tool exists to catch.

---

## Engineering Lead perspective

The harness was built to be small on purpose. `bench-config.ps1` loads one server with one
explicit config, fires a single long-context prompt with a short `n_predict`, reads the
authoritative prefill/decode rates off the response timings, and tears down. No verdict gate,
no telemetry sidecar, no 12-round sweep. Each config iterates in ~3 minutes instead of the
11-hour stress suite, which is the only reason a five-variable elimination was tractable in
one sitting. It was validated against ground truth before any number was trusted: its
Vulkan prefill of 243 tok/s matched the live stress run's 242 exactly.

Two PowerShell 5.1 landmines, both informative. First, `ConvertTo-Json` serialized the 70 KB
prompt string as an *object* (`{"value":...,"Count":...}`) instead of a JSON string - the
known PS 5.1 large/provider-string bug - and llama-server correctly rejected it; fixed by
switching the body serialization to `JavaScriptSerializer`. Second, `-fa off` crashed the
load (`V cache quantization requires flash_attn`), and `-fa off` with f16 KV *timed out
past 600 s* - the non-flash attention path at 25k is catastrophically slow, which is itself
a finding: flash attention is load-bearing here, not optional.

The decode characterization is the part I'm most confident in, because the conclusion rests
on a measurement, not an argument. The shared-memory spill looked like the obvious villain -
the operator's Task Manager screenshot showed one card holding 15 GB in shared system memory.
But a live PDH sample through a single-card run on the *clean* card showed 22.78 GB dedicated,
0.49 GB shared, zero spill, all the way through decode - and decode was still 4.2. That kills
the memory hypothesis cleanly and leaves only the kernel. The new telemetry quirk (SYCL VRAM
invisible to the same PDH counter that reads Vulkan fine) is worth a future disagreement rule;
it pairs directly with the cross-process VRAM signal work.

The SYCL discovery was less engineering than archaeology: enumerate the stack already on disk,
confirm Level-Zero sees both B70s, point the same model/quant/prompt at it, read ollama's
own `prompt_eval`/`eval` durations. The 3.5x replicated four times with essentially no
variance. The one real tradeoff is honest and recorded: SYCL cold-loads 12x slower (131 s vs
11 s), which is irrelevant for hot models held with `keep_alive` and very relevant for
cold-start scripts.

---

## Project / Program Manager perspective

Scope snowballed in the productive direction. The session opened as a one-line bug report
("Ctrl+Alt+Del also BSODs") and became three retirements of real risk plus a measured
performance unlock. None of it was planned at the top; each arc was the natural next question.

The risk ledger moved a lot. **Closed:** the load-test BSOD - the single biggest operational
hazard on this rig, the one that used to threaten BIOS-reflash recovery - is verified fixed
under exact original conditions, with an external corroborating report. **Closed:** "is the
slow long-context decode something we misconfigured?" - no, it's the Vulkan kernel, and we
have a 3.5x alternative. **Opened (small, good):** a SYCL-shaped dependency - IPEX-LLM is
archived upstream as of January 2026, so `b20250630` is approximately the last build; there is
no fresher one coming, which caps that path.

The headline result - 3.5x long-context decode - lands precisely on the operator's stated
workload (Hermes at 125k minimum, coding-model + repo context). That reframes the rig's
strategy: not "make one big model fast across both cards" (the layer-split trap we
characterized), but "run SYCL single-card models, and for two-lane work put one model per card"
- the planner/critic pattern the operator already arrived at by instinct, now the measured
optimum. One prior backlog item is resolved (coopmat, #4 - confirmed no-op). The deferrals are
all written down with reasons: Hermes-125k SYCL validation (needs that model loaded, separate
session), dual-card SYCL (untested - tonight's run was single-card), and the SYCL
shared-memory-ceiling probe (deliberately gated behind the rig's documented VRAM-cascade
hazard).

---

## QA / Verification perspective

The verification spine of this session was **confirm under the hardest conditions, not the
convenient ones.** The BSOD fix was not declared on the easy no-overlay pass; it was declared
only after reproducing the *exact* original crash config - overlay up, composer running,
dual-card load - and firing both triggers. That is the difference between "we stopped doing
the thing that crashed" and "the thing that crashed no longer crashes." The audible GPU stall
on the secure-desktop switch was treated as positive evidence: the hazardous operation still
happens, it just survives now.

The decode conclusion was held to the same bar. Every config delta was a single-variable
change against a validated baseline, and the causal claim (kernel-bound, not memory-bound) was
settled by a *live measurement* that contradicted the attractive hypothesis, not by reasoning
toward a preferred answer. The SYCL win was confirmed n=4 (14.54 / 14.48 / 14.43 / 14.43,
avg 14.47, spread 0.11) with a coherence check on the output text so we weren't celebrating
fast garbage.

Confounds were named at the point of measurement, not after pushback: the first SYCL sample
(13.43) was flagged as carrying cold-load drag before the warm laps settled it at 14.47; the
inflated prefill numbers on laps 2-4 were identified as prompt-cache hits, not real prefill.
What is explicitly **not** verified and labeled as such: 125k context (Hermes's real
requirement - the 25k result is a proxy, not the measurement); dual-card SYCL throughput; and
whether SYCL will use host-memory overflow at all (the gated probe was offered, not run, per
the cascade hazard). The transferable pattern worth naming: *a screenshot is a lead, not a
verdict* - the shared-memory spill was a good thing to chase and a wrong thing to conclude, and
only the PDH probe could tell those apart.

---

## Operator perspective

*(first person - Derek)*

I came in just messing around - I'd found that Ctrl+Alt+Del also dropped the box, and I
thought it was funny more than alarming. What I actually wanted to know, underneath, was
whether the driver I'd reinstalled on the reboot had fixed the thing I'd been fighting for
weeks. So we tested it properly, under the exact conditions that used to kill it, and it held.
That's the one I needed - I can stop babysitting this machine.

The slow decode is the part that matters for what I'm really trying to do. Both these cards
individually beat my 4070 Ti, but Hermes wants a minimum 125k context and coding against a repo
is a lot of context too - so the place where Vulkan falls apart at 4 tok/s is exactly where I
live. When the agent kept landing on "just use short context," I had to say no: long context
*is* the workload. And when it pulled up my own lab notes saying SYCL wasn't a perf candidate,
I made the call to re-test anyway, because that conclusion was from a different driver and I
didn't trust that I'd tested it where it counts. That call paid - 3.5x, sitting in a folder I'd
shelved in May.

A couple of moments I want on the record because they're the whole reason I work this way. The
agent imported a "48 GB per card" number from its own memory notes and started building a
capacity argument on it, and I had to stop it - each B70 is 32 GB, full stop; the rest is a
window into my 32 GB of host RAM. It talked itself all the way around the block to land on
"64 GB pooled," which is the number printed on the front of the box. No fucking shit. That's
not a knock - it's the exact dynamic from my LinkedIn piece, the human keeping the confident
agent honest - but it's also why I don't hand any of these conclusions a blank check. And
honestly: I knew much, much less three weeks ago, and that's probably what let me get through
this build at all. Knowing less meant I didn't pre-concede the things everyone "knows" can't
work on Win10 + Arc + no CUDA. The trick now is not to let my own hard-won conclusions calcify
into the same kind of wall.

---

## How we worked together (human <-> AI)

### What worked well

- **Re-opening a closed door because a variable moved.** The operator's "let's A/B IPEX
  anyway" overrode his own prior "SYCL not a perf candidate" verdict - justified because the
  driver had changed since that verdict. That single decision is the entire reason the 3.5x
  was found. The settled answer was only valid for the conditions it was settled under.
- **Mining old lab notes for the data, not the summary.** The agent read
  `intel_ollama/docs/findings/local-baseline-comparison.md` with no ego in the prior
  conclusion and noticed the data actually showed SYCL *winning* modestly and the advantage
  *widening with output length* - the exact trend that explodes at long context. The README's
  one-line summary ("not a perf candidate") was wrong about its own underlying numbers.
- **Measure before assigning cause.** The shared-memory-spill hypothesis was attractive and
  was disproven by a live PDH probe rather than argued about. The conclusion (kernel-bound)
  survived because it was the last hypothesis standing after measurement, not the first one
  that felt right.
- **The operator's screenshots as a sensor again.** The shared-memory asymmetry that sent us
  down the right diagnostic alley came from him looking at Task Manager; even though it
  resolved to a red herring, it was the correct thing to chase and forced the measurement that
  settled the real cause.
- **Going back through the chaos to find what already existed.** The SYCL stack didn't need
  downloading - it was on disk from a May experiment. Reconstructing that from the build's own
  folders and README saved the whole "install oneAPI" detour.

### What didn't

- **The AI took the scenic route to a number the operator already had on the box.** It
  imported the "48 GB per card" shorthand from its own memory notes, treated it as if it were
  real card memory, inflated the capacity picture, and only un-inflated it back to "64 GB
  pooled VRAM" after the operator corrected it. First-principles arithmetic (32 + 32) should
  have preceded any capacity argument built on a remembered figure.
- **Over-weighting a good lead as a cause.** The shared-memory spill was pursued hard - a full
  hypothesis with PCIe-bandwidth math - before it was measured and found cosmetic. The lead was
  worth chasing; presenting it as the likely cause before the PDH probe was getting ahead of
  the evidence.
- **Mis-framing the workload.** "Match the workload to the rig - use short context" was wrong
  advice for an operator whose real workload is 125k-context Hermes and large-repo coding. The
  optimization target should have been stated and confirmed before recommending around it.
- **Late-session slippage.** By the small hours the AI's precision was visibly fraying (the
  64 GB detour being the clearest case) - which is itself the signal that triggered this retro.

### Patterns to repeat

- Re-test a settled conclusion the moment a variable it depended on changes (driver, context
  regime, build). "We already tried that" has an expiry date.
- Read the *data* in old findings docs, not just their summary lines - summaries drift from
  what the numbers actually said.
- When a screenshot suggests a cause, treat it as a hypothesis and measure it before reporting
  it as the cause.

### Patterns to change

- Never build a capacity or limits argument on a remembered/shorthand number without tracing
  it to first principles first (32 GB + 32 GB = 64 GB, before "48 per card" gets a vote).
- State the workload assumption explicitly *before* optimizing for it - short-context vs
  long-context inverts which backend wins.
- Treat late-session confidence as suspect; when precision starts slipping, wrap up (i.e.,
  run this retro) rather than pushing one more conclusion.

---

## Lessons learned

1. **A "we tried that" is only valid for the conditions you tried it under.** SYCL was shelved
   on an older driver, tested only at short context. Re-tested on `8826` at long context, it's
   a 3.5x win. Re-open settled conclusions when their preconditions move.
2. **Your own lab-note summary can be wrong about your own data.** The README said SYCL "not a
   perf candidate"; the underlying comparison showed it winning and scaling better with length.
   Trust the table over the takeaway when they disagree.
3. **Measure memory placement before blaming it.** A 15 GB "shared memory" bar on one card
   looked like the decode killer and was a cosmetic WDDM shadow. The clean card with everything
   in VRAM decoded identically. Live measurement beats a plausible mechanism.
4. **Optimize for the real workload, not the convenient one.** Short-context advice is wrong
   for a long-context operator; the entire backend recommendation flips on that one fact.
5. **The human re-derives first principles the AI over-builds past.** 32 + 32 = 64. When the
   agent is constructing an elaborate argument around a remembered number, the cheapest check
   is the arithmetic the operator can do in his head.

---

## Next moves

Queued in [`docs/inference-test-backlog.md`](inference-test-backlog.md):

- **SYCL at the real context depth (highest value):** load Hermes (or any 125k-capable model)
  under IPEX-LLM and measure decode at 125k - the 25k result predicts SYCL holds altitude where
  Vulkan collapses, but it's a proxy until measured on the actual model.
- **Dual-card SYCL:** tonight was single-card. Test whether IPEX-LLM can tensor-split across
  both B70s, and whether that helps or just costs (Vulkan layer-split cost prefill, not decode).
- **Planner/critic two-lane, measured:** pin a second SYCL model to `level_zero:1` and confirm
  each lane holds ~14.5 tok/s under simultaneous load - the operator's pattern, end-to-end.
- **Gated SYCL shared-memory-ceiling probe:** does Level-Zero overflow into host RAM or
  hard-stop at VRAM? Run only with the verdict host-floor guardrail; the rig's notes flag
  deliberate max-VRAM as the cascade-to-non-POST hazard.
- **b70tools rule candidate:** SYCL/Level-Zero VRAM is invisible to the PDH `GPU Adapter
  Memory` dedicated counter. Pairs with the cross-process VRAM signal; warrants a
  `source_blind_to_allocation` style disagreement rule so the gate is never trusted on a SYCL
  workload.
- **Carried from 2026-06-16, still open:** clean-room contention-free baseline; 70B
  tensor-split rebalance to stop the shared-memory spill; commit-headroom verdict gate.

---

## Acceptance gates met

- [x] Third BSOD trigger (`Ctrl+Alt+Del` under load) characterized and tested
- [x] Driver fix (`8801` -> `8826`) verified under *exact original* crash conditions (overlay + dual-card load, both triggers, no crash)
- [x] Overlay (`hud.exe`) exonerated as root cause; external corroborating bug report noted
- [x] Long-context decode bottleneck root-caused: Vulkan attention kernel, not config/cards/coopmat/KV/contention/memory
- [x] Shared-memory-spill hypothesis disproven by live PDH measurement
- [x] `bench-config.ps1` micro-bench harness built and validated against the live stress run
- [x] SYCL/IPEX-LLM A/B run; 3.5x long-context decode confirmed n=4 (<1% variance, coherent output)
- [x] New cross-observer telemetry finding logged (SYCL VRAM invisible to PDH counter)
- [~] SYCL shared-memory ceiling - offered, deliberately deferred behind the cascade hazard
- [ ] Hermes-125k SYCL validation - needs the model loaded; separate session
- [ ] Dual-card SYCL throughput - untested (tonight was single-card)
