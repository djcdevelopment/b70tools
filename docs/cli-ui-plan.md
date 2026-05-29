# b70tools — CLI / TUI bridge plan

**Status:** plan + Phase 1 underway (`summarize` lands in this commit).
**Date:** 2026-05-28.
**Position in project:** sits between v1's working telemetry runtime (M2 PASS on real rig) and the eventual GUI vision (sketched in `D:\work\b70tools\UIdesign\`).

---

## 1. Why CLI/TUI first

`D:\work\b70tools\UIdesign\` defines a five-surface GUI (S0 approach, S1 session view, S2 adapter detail, S3 replay & comparison, S4 cost of observation). It is **reference UX, not the next implementation target**. The reasoning:

- Plan v2 Rule 1 ("lightweight overrides comprehensive") and Rule 3 ("telemetry value beats platform engineering") both push back on shipping a GUI before CLI workflows are proven.
- The do-no-harm budget (RSS < 50 MB, headroom 8–12 GB shared with the inference workload) is incompatible with any framework above native Win32.
- The UI design's own house rule — **"the live view is the replay view at T = now (same renderer)"** — is exactly the boundary that the JSONL/view-model split makes natural. CLI commands force us to define that contract before we paint pixels.
- Information architecture deserves validation against real telemetry first. The UI mockups are sketches; the data is real. CLI surfaces let us iterate on the mapping cheaply.

**Operating principle (carried forward from §A.1 of plan.md):** `b70tools.exe` stays a lightweight collector/runtime. Any future GUI is a **separate process** that reads `events.jsonl` or a summarized view-model stream. The UI must never be required for data capture; if it crashes or balloons, telemetry keeps running.

---

## 2. Renderer memory-cost matrix

Rough resident-memory cost for each plausible renderer hosting the same view model, on a Windows 10 19045 host with the inference workload running:

| Renderer | Resident memory | Notes |
|---|---|---|
| **Plain CLI (printf only)** | **~5–15 MB** | What we have today. Pipe-friendly, copy-pasteable, no rendering loop. |
| **ANSI/VT terminal (raw escape codes)** | **~5–20 MB** | Windows Terminal / conhost VT-on. Tables + sparklines without a TUI lib. |
| **TUI library (FTXUI, header-only C++)** | **~10–25 MB** | Same process model as today; widgets, layout, keyboard input. Best fit for the live console view. |
| **PDCurses** | **~5–15 MB** | Older but proven; less ergonomic API than FTXUI. |
| Native Win32 (GDI/Direct2D) | ~20–50 MB | Manual painting, fully native. Possible but high build cost vs value. |
| WPF (.NET) | ~150–300 MB | CLR + WPF runtime. **Exceeds do-no-harm budget alone.** |
| WinUI 3 (.NET / WinAppSDK) | ~150–250 MB | Same problem, fancier. |
| WebView2 (Edge embedded) | ~80–400 MB | Browser process pool. Variable, can exceed budget. |
| Electron | ~250–600 MB+ | Chromium full stack. Disqualified outright on this host. |

**Recommendation:** stay on plain CLI through Phase 4. If/when a live TUI is justified by user workflow, adopt **FTXUI** (header-only, C++17, ~15 MB). Defer any GPU/web rendering until CLI usefulness is exhausted, and even then host it in a **separate process** that consumes the view-model JSON (Section 4).

**Hard rule:** the `b70tools.exe` process never embeds a renderer above "plain CLI" tier without an explicit `--ui` flag and a separate-process IPC boundary.

---

## 3. UI surface → CLI command mapping

The five UI design surfaces map to CLI verbs as follows. Each verb is text-first, copyable, useful in articles/reports.

| UI surface | CLI verb(s) | Phase |
|---|---|---|
| **S0 approach** | implicit in `b70tools --help` + this doc | n/a |
| **S1 session view** (live) | `b70tools watch --out <run>` (Phase 2) | Phase 2 |
| **S1 session view** (recorded snapshot) | `b70tools summarize <run>` | **Phase 1** |
| **S2 adapter detail** | `b70tools adapters <run>` | Phase 1.5 |
| **S2 fingerprint tab** | `b70tools fingerprint <run>` (or fold into `summarize --verbose`) | Phase 1.5 |
| **S2 collectors tab** | `b70tools collectors <run>` (or `summarize --collectors`) | Phase 1.5 |
| **S3 replay & comparison** | `b70tools replay <run> [--at T] [--events] [--jump disagreement]` | Phase 3 |
| **S3 overlay/compare** | `b70tools compare <runA> <runB>` | Phase 4 |
| **S4 cost of observation** | `b70tools self <run>` | Phase 1.5 |
| **S4 perturbation events** | folded into `self` and `disagreements` | Phase 1.5 |

`disagreements <run>` is a focused view; it complements `summarize` and lets the user `b70tools disagreements run | less`.

---

## 4. View-model JSON: the GUI boundary

Phase 5 is "future GUI adapter." The renderer must read a **stable view-model JSON**, not the raw `events.jsonl`. The CLI commands produce this same shape, so the same renderer can serve both live and replay.

Sketch (refined as Phases 1–4 land):

```json
{
  "format": "b70tools-view-model-v0",
  "session": {
    "epoch": 0,
    "events_total": 77,
    "ticks": 3,
    "duration_qpc_ns": 197215700,
    "fingerprint": { "windows": "...", "intel_driver": "32.0.101.8801", "hags": true, "warnings": ["..."] }
  },
  "adapters": [
    {
      "id": "adapter_00011b4f",
      "name": "Intel(R) Arc(TM) Pro B70 Graphics",
      "luid_hex": "0x0000000000011b4f",
      "pci_bdf": "0000:0c:00.0",
      "driver_uuid_decoded": "32.0.101.8801",
      "dedicated_vram_bytes": 34215034880,
      "shared_system_bytes": 17137704960,
      "task_mgr_sum_hint_bytes": 51352739840,
      "state_reached": "Idle",
      "peak_metrics": [ { "name": "vram.local.current_usage_bytes", "value": 4096, "unit": "Bytes", "source": "DXGI_VideoMemoryInfo" } ]
    }
  ],
  "collectors": [
    { "name": "vulkan_memory_budget", "declared": "DriverPassive", "app_passive": true, "init_wall_ns": 52606000, "rss_delta_bytes": 17358848, "threads_added": 0, "modules_added": 15, "third_party_layers": ["RTSSVkLayer64.dll"], "observed_undeclared": false }
  ],
  "disagreements": [
    { "rule": "expected_source_unavailable", "adapter_id": "adapter_00011b4f", "explanation": "...", "sources": ["D3DKMT_PerfData"] }
  ],
  "observation_cost": { "rss_peak_bytes": 33000000, "tick_p50_ns": 0, "events_per_sec": 0 }
}
```

`b70tools summarize <run> --json` will emit this in Phase 1.5. The plain-text summarizer (this commit) prints a humans-only version of the same shape.

---

## 5. Phased plan

### Phase 1 — Summary CLI (**this commit**)

- [x] `b70tools summarize <run>` — structured text report, the seven questions in the user's brief.
- [ ] `b70tools adapters <run>` (Phase 1.5)
- [ ] `b70tools disagreements <run>` (Phase 1.5)
- [ ] `b70tools collectors <run>` (Phase 1.5)
- [ ] `b70tools self <run>` (Phase 1.5)
- [ ] `b70tools fingerprint <run>` (Phase 1.5)
- [ ] `--json` output mode on all verbs above (Phase 1.5)

### Phase 2 — Live console view

- `b70tools watch --out <run>` — pretty-prints the same summary, refreshing every `--cadence-ms`. Reads the *live* events.jsonl as it's being written.
- Or `b70tools tui --run <run>` if we adopt FTXUI. Same data, denser layout.
- Both processes are **siblings** of a `--run` instance, not embedded in it. The collector keeps running if the viewer dies.

### Phase 3 — Replay CLI

- `b70tools replay <run>` — scrub a recording, default to start.
- `b70tools replay <run> --at <ISO-timestamp-or-QPC>` — print the system state at that point.
- `b70tools replay <run> --events` — chronological event log.
- `b70tools replay <run> --jump disagreement` — print every event around each `DisagreementReport`.

Mirrors the UI design's "scrub a recording" affordance without painting.

### Phase 4 — Comparison CLI

- `b70tools compare <runA> <runB>` — fingerprint diff, per-adapter memory deltas, activity deltas, state transitions, disagreement diff, observation-cost diff.
- Output sections mirror the UI's S3 surface ("aligned at workload-begin" etc.) — alignment cues are written into the text rather than rendered.

### Phase 5 — Future GUI adapter (no commit until justified)

- Stable `view-model-v1` JSON spec (Phase 1.5 `--json` is the seed).
- Renderer reads view-model JSON only; never touches `events.jsonl` directly.
- Same renderer model serves live (tail the JSONL via `summarize --watch --json`) and replay.
- UI is optional, never required for data capture.

---

## 6. What's intentionally NOT here

- Color theming, layout iteration, custom fonts.
- Sparklines in the CLI (Phase 2 may add ASCII histograms; Phase 1 is plain values).
- Subprocess management, daemon mode, IPC sockets.
- A `--gui` flag on `b70tools.exe`.
- Any framework choice for the future GUI.

When workflows mature enough that one of these is justified by use, it's a v2 step, not a v1.5 step.
