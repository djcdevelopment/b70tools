# WoW impact harness

Phase-1 scripts for `docs/wow-realtime-inference-impact-plan.md`.

## Baseline run

Run b70tools plus host-pressure capture for a fixed window:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\wow-impact\Start-WowImpactRun.ps1 `
  -Scenario "wow-only-baseline" `
  -DurationSec 300 `
  -TailWowLog `
  -WowLogPath "C:\path\to\World of Warcraft\_retail_\Logs\WoWCombatLog.txt" `
  -WowLogTailIntervalSec 7 `
  -DisplayAdapterId "adapter_..." `
  -InferenceAdapterId "adapter_..."
```

The script creates `runs\wow-impact-<timestamp>-<scenario>\` and writes:

- `manifest.json`
- `events.jsonl`
- `host-pressure.jsonl`
- `wow-log-tail.jsonl` when `-TailWowLog` is set
- `b70tools-*.txt`
- `notes.md`

## Start the second-B70 llama-server

When another builder asks "how do I start the llama server?", use this launcher:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\wow-impact\Start-SecondB70LlamaServer.ps1
```

The default launch is intentionally single-card:

- `GGML_VK_VISIBLE_DEVICES=1`
- `GGML_VK_DISABLE_COOPMAT=1`
- model: `D:\work\battlemage\models\qwen2.5-14b-instruct-q4_K_M.gguf`
- server: `http://127.0.0.1:8080`
- alias: `tempo-b70-second`

If a boot or driver update proves the cooler/non-display card is exposed as a
different Vulkan index, pass that single index explicitly:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\wow-impact\Start-SecondB70LlamaServer.ps1 `
  -VisibleDevices 0 `
  -Port 8080 `
  -Alias tempo-b70-second
```

Do not use `GGML_VK_VISIBLE_DEVICES=0,1`, `-sm layer`, or `-ts 1,1` for live WoW
testing. Those are dual-card experiments and can touch the display/gaming card.

After launch, copy the emitted settings into `%LOCALAPPDATA%\Tempo\settings.json`
or use the generated `tempo-settings-snippet.json` from the run directory:

```json
{
  "llmProvider": "LocalLlamaServer",
  "lanternBackend": "LlamaServer",
  "llamaServerBaseUrl": "http://127.0.0.1:8080",
  "llamaServerModel": "tempo-b70-second"
}
```

## Inference stimulus

During an active run, send a bounded request to an already-running llama-server:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\wow-impact\Invoke-InferenceStimulus.ps1 `
  -RunDir .\runs\wow-impact-<timestamp>-post-fight `
  -Mode Server `
  -BaseUrl http://127.0.0.1:8080 `
  -Label "encounter-end" `
  -PromptFile .\prompts\post-fight-smoke.md `
  -MaxTokens 256
```

For CLI mode, bind inference to the intended second B70:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\wow-impact\Invoke-InferenceStimulus.ps1 `
  -RunDir .\runs\wow-impact-<timestamp>-post-fight `
  -Mode Cli `
  -LlamaCliBin D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe `
  -ModelPath D:\work\battlemage\models\<model>.gguf `
  -VisibleDevices 1 `
  -Label "cli-second-card" `
  -PromptFile .\prompts\post-fight-smoke.md `
  -MaxTokens 256
```

Each request appends one JSONL row to `inference-requests.jsonl`.

## Summarize

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\wow-impact\Summarize-WowImpactRun.ps1 `
  -RunDir .\runs\wow-impact-<timestamp>-post-fight
```

If a frame-time CSV exists at `frame-times.csv`, or if `-FrameCsvPath` points to
one, the summarizer reports p50/p95/p99 and long-frame counts. Without frame
times, the run can validate the harness but cannot answer game impact.

## PresentMon

`Start-WowImpactRun.ps1` can start a local PresentMon binary, but the script does
not assume a specific PresentMon build or argument schema. Pass the exact local
arguments with `-PresentMonPath` and `-PresentMonArgs`, and make sure they write
CSV to the run directory or pass the resulting path to the summarizer.
