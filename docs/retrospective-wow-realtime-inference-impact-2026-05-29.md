# retrospective - WoW realtime inference impact round 1

**Date:** 2026-05-29
**Scope:** first live Mythic+ load test for second-B70 inference during gameplay.

## What worked

- The collaboration model worked: trust the player report, but verify with
  telemetry.
- Headless capture was acceptable. No overlay was needed.
- The combat-log tailer captured real activity once we stopped assuming the file
  was inactive and verified it at run time.
- The 7-second log sampler is useful. It caught 140,750 new lines and a peak
  18,048-line burst in one sample.
- The user completed the key without reporting noticeable slowdown, even while
  the stressor was active.
- b70tools overhead stayed inside the existing do-no-harm budget.

## What failed or got messy

- The first assumption about combat logging was wrong. The file was not moving
  during an earlier check, but it later resumed. The correct statement was "not
  moving right now," not "we missed the log."
- Background process orchestration was fragile in this shell environment. The
  reliable path was foreground capture; interrupted foreground runs can leave
  child processes alive.
- The first key-run orchestrator had parameter-splatting and quoting bugs that
  were only exposed under live use.
- The cold `llama-cli` stressor did not complete useful output.
- Host RAM was pushed too hard: free RAM reached `0.24 GB`.
- No frame-time data was captured, so the strongest evidence remains human game
  feel plus telemetry, not p95/p99 frame pacing.

## Good decisions

- Treating this as load testing rather than output-quality testing kept the
  result clear.
- Starting with 5-player content was the right risk level.
- Tail sampling from current file end avoided reprocessing huge logs.
- Keeping artifacts in the b70tools repo made the run auditable.

## Bad assumptions to retire

- "If the log has not moved for 30 seconds, logging is off." It may simply be
  out of combat or between flush bursts.
- "One-shot llama-cli is a reasonable first live inference shape." It is useful
  as a stressor, but it confounds model-load cost with inference cost.
- "No completed response means failed test." For tonight, the goal was load, not
  useful output.

## Updated operating model

For live tests, the player report is the first-class pass/fail signal for game
feel. Telemetry is the receipt.

Pass means:

- player reports no hitching, input delay, audio chop, or meaningful slowdown;
- WoW remains stable;
- telemetry shows no driver reset or obvious system-edge condition.

Tonight was player-pass but system-pressure yellow/red because RAM got too low.

## Next implementation fixes

1. Add a safer run-stop command or PID manifest cleanup helper.
2. Make `Start-WowKeyRun.ps1` append child process stdout/stderr to files with
   visible error records.
3. Add explicit "model-load stress" vs "hot inference request" labels in the
   manifest.
4. Add optional PresentMon integration once the local CLI path and args are
   known.
5. Improve summarizer output around process memory peaks and stimulus timing.

## Next experiment

Run the same 5-player shape with a preloaded `llama-server`:

- load model before group starts;
- verify host RAM floor before the pull;
- start passive capture;
- issue one bounded completion at the 7-minute mark;
- compare RAM and game feel against tonight's cold-load result.

After that, test raid as a new tier. Raid is not just "bigger Mythic+"; it has a
different combat-log volume, render load, and failure sensitivity.

