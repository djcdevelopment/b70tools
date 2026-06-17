# Stream B status - b70tools (run-dir launcher + residency/verdict)

**Branch:** `research-gauge/b70tools` (Cowork session auto-worktree).
**Builder:** fresh Cowork session - paste `docs/agent-guides/research-gauge-stream-b-kickoff.md`.
**Orchestrator:** main session (contract owned by the discoverlay repo).
**Role:** G3 - research-run launcher, bind by stable physical identity (never persist vk:N),
settle-and-sample residency -> `verdict.json` (`binding_status`), game-impact telemetry. `[CONTRACT-ACK]`
after discoverlay posts `[CONTRACT-READY]`.

Append one line per checkpoint. Signals: `[RESEARCH-DONE]` · `[G3-GREEN]` · `[CONTRACT-ACK]` ·
`[BLOCKED: <symptom>]` · `[NEEDS-CONTRACT-CHANGE: <what>]` · `[COMPLETE] <SHA>`.

- 2026-06-01: skeleton created - awaiting builder session.
- 2026-05-31T22:14:52-07:00: started.
- Research findings:
  - `scripts/wow-impact/Start-WowImpactRun.ps1` creates the run dir, starts host-pressure capture, optional WoW log tailing, optional PresentMon, then launches `b70tools run`; it writes `manifest.json`, `events.jsonl`, `host-pressure.jsonl`, `wow-log-tail.jsonl`, and post-run analysis artifacts.
  - `scripts/wow-impact/Start-SecondB70LlamaServer.ps1` is the single-card llama-server launcher; it currently binds with `GGML_VK_VISIBLE_DEVICES`, writes a manifest, and emits the Tempo settings snippet.
  - `src/identity/reconciler.cc` is the stable identity machinery: DXGI + SetupAPI + Vulkan are reconciled by LUID, with PCI BDF and driver UUID evidence retained in adapter bindings.
  - PresentMon already has a slot in the harness via `Start-WowImpactRun.ps1`; `Summarize-WowImpactRun.ps1` reports frame-time p50/p95/p99 when a CSV is present.
  - BOM audit: the wow-impact scripts currently persist JSON/JSONL via `Out-File -Encoding UTF8` or `StreamWriter(..., Encoding.UTF8)`, which will emit a UTF-8 BOM on Windows PowerShell 5.1. Offending JSON/JSONL writes include `manifest.json`, `summary.json`, `key-run-launch.json`, `tempo-settings-snippet.json`, `inference-requests.jsonl`, `host-pressure.jsonl`, `wow-log-tail.jsonl`, and the `Start-SecondB70LlamaServer.ps1` manifest/stdout paths.
- [RESEARCH-DONE]
- 2026-05-31T22:54:02-07:00: [CONTRACT-ACK]
- 2026-06-01: **[G3-GREEN — orchestrator takeover]** Builder credits exhausted; the main session verified and finished Stream B. `Start-ResearchGaugeRun.ps1` is COMPLETE: create-first run dir (never `D:\tmp`); `b70tools --enumerate` → parse `ai` records for stable identity (LUID / PCI-BDF / driver-UUID) + Vulkan index; resolve target by **stable identity** (never persists `vk:N`); resolve gaming card; launch `Start-SecondB70LlamaServer` bound to the resolved vk index; bounded stimulus; **settle-and-sample** residency (`Get-ResidencyVerdict` — delta/stability thresholds, **MISMATCH when the largest residency delta lands on the wrong card**); schema-conformant `verdict.json`. Game-impact telemetry captured (DPC watcher + optional PresentMon frametimes).
  - Crash root-caused: b70tools.exe `main.cc` calls the **throwing** `create_directories` ×3 with no handler → silent `0xC0000409` when `--out`'s parent is unwritable (e.g. the protected `D:\tmp`). The launcher's create-first behavior avoids it. Robustness fix (→ non-throwing + clean stderr) recommended but **not applied** here, to avoid commingling with the dirty research-wow `main.cc`.
  - **Verification (orchestrator):** all 10 wow-impact scripts parse clean; `--slow-cadence-ms` / `--enumerate` / `run` flags confirmed in `main.cc`; verified / MISMATCH / unresolved `verdict.json` variants serialize **no-BOM** via `Write-JsonNoBom` and **all validate against discoverlay's frozen `research-feed-verdict.schema.json`**.
  - Added a **`-DryRun`** switch (enumerate + resolve + `dry-run-report.json`; no inference / telemetry / verdict) for hazard-safe resolution verification.
  - **Live run is the only unverified part** (binds llama-server on a real B70) — operator-driven, per the dual-card hazard rules (b70tools first, single-card, comfortable VRAM headroom).
  - **Commit left to the operator:** the launcher sits on top of the untracked wow-impact harness + uncommitted `src/` on `research-wow-realtime-inference-impact`; committing it cleanly belongs with that harness, not a partial isolated branch.
- 2026-06-01: **[-DryRun live-verified on the real rig]** `--enumerate` → `adapter_00011506` (pci-bdf `0000:0c:00.0`, vk0) + `adapter_00012a22` (pci-bdf `0000:10:00.0`, vk1). The adapter_ids **drifted again** from last session's `00011631`/`00012b15`, yet `-DryRun -RequestedIdentity pci-bdf:0000:0c:00.0` resolved correctly by **stable BDF** (→ adapter_00011506, would-bind vk:0, gaming=adapter_00012a22) and wrote `dry-run-report.json` with no inference/crash. Invariant #2 (bind by stable identity, not the drifting LUID/vk) **proven live.** Remaining: inference-bind + settle-and-sample verdict on a full live run.

- 2026-06-01T19:17:13.5156049-07:00: [RESEARCH-DONE] Confirmed plan gate and current gap: only src/collectors/pdh_gpu_memory.{cc,h} exist; pdh_gpu_engine_lite is planned but unimplemented. PDH GPU Engine instances use luid_0x<HighPart>_0x<LowPart>_phys_<n>_eng_<n>_engtype_<kind> style names, so per-adapter summation should bind by parsed LUID and sum all engine types. src/schema/jsonl_writer.cc uses _IOFBF with a 64 KiB buffer and only flushes on close, which explains the live events.jsonl lag.

- 2026-06-01T19:29:44.9472077-07:00: [P1-COLLECTOR-GREEN] Added PassiveSafe pdh_gpu_engine using PDH \\GPU Engine(*)\\Running Time deltas grouped by parsed LUID/phys/eng and emitted gpu.engine.utilization_pct for both adapters. Idle baseline run uns/pdh-engine-idle-baseline-1 shows poll cost avg 1167.8 us/tick (~0.12% of one core at 1 Hz), init RSS +760 KiB, and full observation RSS still ~21.1 MiB — inside the do-no-harm budget. Raw counter inspection on this rig showed the nonzero adapter was 3D-dominant while the flaky-IGCL adapter still emitted 0.0 via PDH.
- 2026-06-01T19:29:44.9472077-07:00: [FLUSH-GREEN] events.jsonl now flushes per tick for unbounded runs (--ticks 0), with bounded/replay behavior unchanged by default. Live check uns/live-flush-1 grew on disk during the active run at 3 s / 6 s / 9 s (120 / 139 / 171 lines; last-write timestamps stayed within a few seconds). Diagnostic cost check uns/live-flush-bench-1 measured 10 flushes at avg 47.1 us, max 139.1 us, total 0.471 ms.

- 2026-06-01T19:31:10.7384325-07:00: [COMPLETE] cb258b8

- 2026-06-01T19:43:47.0762268-07:00: [P1-COLLECTOR-GREEN] ADJ-4 hardened gpu.engine.utilization_pct clamping at emission with a regression note: concurrent engine-family sums may exceed 100 under gaming load, but emitted values are now always clamped to [0,100] so discoverlay will not fall back to degraded IGCL. Re-verified in runs/pdh-engine-clamp-verify-1: adapter_0001151c max=64.4794266834158, adapter_00012a98 max=0.0, both <=100.
