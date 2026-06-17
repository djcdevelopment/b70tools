# findings - WoW realtime inference impact, round 1

**Date:** 2026-05-29
**Branch:** `research-wow-realtime-inference-impact`
**Run:** `D:\work\b70tools\runs\wow-impact-20260529-055848-key-live`
**Scenario:** live Mythic+ key, 5-player gameplay, combat logging enabled.
**User-facing result:** the player reported no noticeable gameplay impact and completed the key successfully.

## Question

Can we run a meaningful second-B70 load during live WoW gameplay without the
player noticing?

For this first live round, "meaningful load" was deliberately rough: a cold
`llama-cli` launch of `qwen2.5-14b-instruct-q4_K_M.gguf` bound to one B70 seven
minutes into the run. Useful model output was not expected. This was a load
boundary probe, not a coaching-quality test.

## Test shape

Passive capture started at `2026-05-29T05:58:48-07:00`:

- `b70tools run` telemetry.
- Host pressure JSONL.
- WoW combat-log tail every 7 seconds.
- No UI overlay.

Scheduled stressor:

- Delay: 420 seconds after run start.
- Binary: `D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe`.
- Model: `D:\work\battlemage\models\qwen2.5-14b-instruct-q4_K_M.gguf`.
- Binding: `GGML_VK_VISIBLE_DEVICES=1`.
- Max generation target: 160 tokens.

The stressor started around `06:05:48`. It did not complete before it was stopped
after the instance ended.

## Headline result

**Game-feel pass, system-pressure yellow/red.**

The practical player-facing result was excellent for a 5-player key: the user
reported no noticeable slowdown while the key was in progress, despite the
stress test driving host memory extremely hard.

The engineering result is more cautious: cold-loading a GGUF with one-shot
`llama-cli` during gameplay is not the right final shape. It caused a large host
commit jump and pushed free RAM to `0.24 GB`.

## Evidence captured

### Combat-log activity

The combat log was active and heavily moving during the capture:

| Metric | Value |
|---|---:|
| Tail samples | 184 |
| New combat-log lines | 140,750 |
| New combat-log bytes | 42,922,481 |
| Largest 7s line burst | 18,048 lines |
| Last observed combat-log event | Hearthstone cast at `06:18:12.988-7` |

This confirms the capture covered the real key, not an idle menu or post-run
window.

### Host pressure

| Metric | Value |
|---|---:|
| Host-pressure samples | 418 |
| Minimum free RAM | 0.24 GB |
| Maximum used RAM | 31.681 GB |
| Commit delta | +15.249 GB |
| Commit at low-RAM point | 46.707 GB |

At the minimum-free-RAM point (`06:07:23`):

| Process | Working set | Private memory |
|---|---:|---:|
| WoW | 3,583.96 MB | 11,706.31 MB |
| `llama-cli` | 678.16 MB | 15,460.52 MB |

The `llama-cli` process was still present at the end of capture with roughly the
same private memory footprint. The stressor did not produce a completed
`inference-requests.jsonl` row because it was still running when stopped.

### GPU telemetry

b70tools captured about 21 minutes of telemetry:

| Metric | Value |
|---|---:|
| Telemetry span | 1,279.51 s |
| JSONL size | 3,538,944 bytes |
| JSONL rate | 2,764.7 B/s |
| b70tools collector-attributed RSS | 20.1 MiB |
| do-no-harm RSS budget | PASS |

Primary active adapter (`adapter_00010ef0`):

| Metric | Value |
|---|---:|
| Render/compute activity | 66.3% |
| Global activity | 71.9% |
| GPU temp | 56-76 C |
| VRAM temp | 62-70 C |
| GPU freq peak | 2.750 GHz |

Second adapter (`adapter_0001293e`) remained in the known broken-IGCL telemetry
state:

- activity counters advanced at physically impossible rates;
- voltage/frequency emitted the known impossible values;
- 525 `physically_impossible_frequency` reports;
- 525 `physically_impossible_voltage` reports.

This prevents a clean per-card inference-utilization claim from b70tools alone.
The host-pressure cliff, process table, and scheduled stimulus timestamp are the
stronger evidence that the model load happened.

## What this proves

1. b70tools plus host-pressure capture plus combat-log tailing can run during a
   real Mythic+ key without reported player-visible impact.
2. A cold `qwen2.5-14b` `llama-cli` load can be introduced mid-key without the
   player noticing in this 5-player test.
3. Combat-log tailing at 7-second cadence works and captures bursty real gameplay
   write patterns.
4. The machine has more practical 5-player headroom than expected.

## What this does not prove

1. It does not prove frame pacing was unaffected. No PresentMon or frame-time CSV
   was captured.
2. It does not prove the inference was isolated cleanly to the intended B70.
   Telemetry on `adapter_0001293e` is degraded.
3. It does not prove useful coaching quality. The prompt was a synthetic bounded
   stimulus.
4. It does not prove the cold-load shape is safe. Free RAM fell to `0.24 GB`,
   which is too close to the system edge for a repeatable workflow.

## Practical conclusion

For 5-player content, the rough load test passed by game feel.

For production-shaped testing, do not cold-load GGUF models with one-shot
`llama-cli` during gameplay. The next test should use a preloaded server or a
smaller always-hot model so model loading is separated from inference cost.

## Next test shape

1. Start passive capture before the key.
2. Preload `llama-server` before combat begins, or use a much smaller hot model.
3. At the 7-minute mark, issue a bounded request against the already-loaded
   server.
4. Capture frame pacing if possible.
5. Repeat the same structure in raid as a separate escalation tier.

