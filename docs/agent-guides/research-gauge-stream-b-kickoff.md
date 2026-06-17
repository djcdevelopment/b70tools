# Research-Gauge — Stream B (b70tools) kickoff (paste-ready)

Open a fresh Claude Code session in the Cowork desktop (auto-worktree on
`research-gauge/b70tools`). Paste the fenced block below as the first message.

The canonical plan lives in the **discoverlay** repo (you can't read it from here), so your
section + the shared contract are reproduced **verbatim** below and are authoritative for you.
Contract changes route through the operator → orchestrator.

---

```
You are Stream B of the Research-Gauge cross-repo build. Repo: b70tools
(D:\work\b70tools). You own the research-run LAUNCHER and the residency/verdict truth:
binding inference to the correct physical Intel B70 and proving which card actually ran.

Working dir is the auto-created worktree. Branch: research-gauge/b70tools.
Push freely to YOUR BRANCH. NEVER push to main/master. git fetch before each push.
Full permissions; proceed autonomously; escalate only when BLOCKED.

ARCHITECTURAL INVARIANTS:
 1. Instrument, not verdict.
 2. ADAPTER IDENTITY IS A LAUNCH INVARIANT — bind by STABLE PHYSICAL IDENTITY (PCI BDF /
    device UUID / adapter LUID), RE-RESOLVED EVERY LAUNCH. NEVER persist vk:N (Vulkan
    enumeration order is not stable across reboot/driver). Residency only VERIFIES the binding.
 3. Reproducibility = recorded, not promised.
 4. Measurement firewall (Tempo's concern).
 5. Parallel research, serialized integration — you are step 2 (after discoverlay's contract).

START SEQUENCE — in order:
 1. Create docs/research-gauge-stream-b-status.md if absent (skeleton at bottom). Append "<ts> started".
 2. RESEARCH (write findings to status; then [RESEARCH-DONE]):
    a. How scripts\wow-impact\Start-WowImpactRun.ps1 creates a run dir + writes manifest/events/
       host-pressure/wow-log-tail. Reuse this for a research-run dir.
    b. How Start-SecondB70LlamaServer.ps1 launches single-card llama-server (tempo-b70-second@8080).
    c. The existing identity reconciliation (src/identity/reconciler.cc — DXGI/Vulkan/SetupAPI ->
       LUID/BDF). This is your stable-identity machinery; reuse it.
    d. Existing PresentMon / frame-time capture in the harness (Start-WowImpactRun can start
       PresentMon; Summarize reports p50/p95/p99). This is your game-impact-telemetry starting point.
    e. AUDIT existing JSON writes: the wow-impact *.ps1 use `Out-File -Encoding UTF8`, which on
       Windows PowerShell 5.1 emits a UTF-8 BOM. A BOM breaks the Python composer (json.loads on
       the first line) and the C++ overlay reader. Record every offending write.

 3. G3 — HARDWARE SAFETY PREFLIGHT + LAUNCHER (your main deliverable):
    - A research-run launcher (complete .ps1) that: creates D:\work\b70tools\runs\research-<stamp>-<slug>\;
      starts telemetry capture into it; binds inference to the NON-GAMING Intel card by STABLE
      identity, re-resolved at launch (resolve stable-id -> current vk index AT LAUNCH ONLY, pass to
      llama.cpp, never store vk:N).
    - SETTLE-AND-SAMPLE residency: poll local VRAM to stable (threshold + retries), not a single
      before/after read. Then write verdict.json (shape below). requested != resolved =>
      binding_status MISMATCH (a FAILURE state, not a note); the launcher MAY REFUSE to proceed.
    - Capture game-impact telemetry (frametimes / present / DPC) DURING the run so "safe cadence"
      is MEASURED, not asserted. Write it into the run dir.
    - Append [G3-GREEN].
 4. After discoverlay posts [CONTRACT-READY], reconcile verdict.json to the frozen schema and
    append [CONTRACT-ACK]. (Build against the DRAFT below until then.)

SHARED CONTRACT (research-feed-v1, DRAFT pending freeze at G2 — authoritative for you):
  Shared run dir: D:\work\b70tools\runs\research-<stamp>-<slug>\ holds run.json + scores.jsonl
  (Tempo), events.jsonl + frame-impact.* + verdict.json (you), per-run.
  YOU WRITE verdict.json:
    { "schema_version":"research-verdict-v1", "run_id":"...", "ts":"<ISO>",
      "binding_status":"verified"|"MISMATCH"|"unresolved",
      "requested_identity":"pci-bdf:0000:0c:00.0", "resolved_adapter":"adapter_<luid>",
      "evidence":"settle-and-sample: local VRAM +14.9 GB over 3 polls on adapter_<luid>",
      "gaming_adapter":"adapter_<luid2>", "failure_reason": null }
  JSONL/JSON DISCIPLINE (hard): every JSON/JSONL write uses
    [System.IO.File]::WriteAllText($path, $json, [System.Text.UTF8Encoding]::new($false))
    -- NEVER Set-Content / Out-File for JSON. JSONL appends are line-atomic (one full line + `\n`,
    single write). Readers tolerate a partial trailing line.

TERRITORY (OWNED): scripts\wow-impact\** (+ a new research-run launcher), the verdict/residency
  path, docs/research-gauge-stream-b-*.md. Additive CLI knobs only if needed.
TERRITORY (FORBIDDEN): other repos; committed v1 collector BEHAVIOR beyond additive needs; main.
SAFETY: keep docs/operational-runbook.md dual-card hazard rules — single-card default; b70tools
  first; comfortable VRAM headroom; NO intentional max-VRAM stress.

ACCEPTANCE:
  .\build.ps1                                  # green
  One command: stands up the second-B70 target on the NON-GAMING card + creates the research run
  dir + emits a BOM-FREE verdict.json whose resolved_adapter matches settle-and-sample residency.
  Forced-MISMATCH test: launcher REFUSES (binding_status=MISMATCH).
  All emitted JSON is UTF-8 no-BOM (verify with a byte check).

STATUS / SIGNALS — append to docs/research-gauge-stream-b-status.md:
  [RESEARCH-DONE] · [G3-GREEN] · [CONTRACT-ACK] (after D's [CONTRACT-READY]) ·
  [BLOCKED: <symptom>] · [NEEDS-CONTRACT-CHANGE: <what>] · [COMPLETE] <SHA>

PROTOCOLS:
  - Deliver COMPLETE .ps1 scripts (one command per line; PS 5.1). Small commits; re-read before commit.
  - Do NOT diverge from the contract above. If it can't work, write [NEEDS-CONTRACT-CHANGE: <what>]
    + HALT; the operator brings it to the orchestrator (who owns the contract in discoverlay).
  - If stuck after ~3 attempts: [BLOCKED: <symptom>] + HALT.

STATUS SKELETON (create if absent):
  # Stream B status — b70tools (run-dir launcher + residency/verdict)
  Branch: research-gauge/b70tools. Builder: this Cowork session. Orchestrator: main session.
  Append one line per checkpoint; final line is [COMPLETE] <SHA>.
  - <ts>: skeleton created.

Proceed. Start with the research tasks.
```
