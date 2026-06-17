# Start-StressSuite.ps1
#
# Multi-model load-test sweep against the parsed combat-log corpus.
# Drives the discoverlay overlay showing live GPU telemetry + test output.
# The COMBAT panel is repurposed as a tailed STRESS TEST output window.
#
# Run this and walk away.  It will:
#   1. Start b70tools GPU telemetry (continuous, for the overlay)
#   2. Start Watch-HostPressure sidecar (RAM/disk/process tracking)
#   3. Start hud.exe (the overlay renderer)
#   4. Start the discoverlay composer in --stress-mode (tails telemetry + feed)
#   5. For each model (small to large): run the full eval suite, stream
#      progress to the STRESS TEST panel, capture per-model stats
#   6. Write a summary when all models are done
#
# Usage:
#   .\Start-StressSuite.ps1
#   .\Start-StressSuite.ps1 -SkipHud            # overlay already running
#   .\Start-StressSuite.ps1 -Models qwen2.5-14b-q4,llama3.3-70b-q4
#   .\Start-StressSuite.ps1 -DryRun             # print plan, start nothing

param(
    [string]$B70Tools         = 'D:\work\b70tools\build\b70tools.exe',
    [string]$LlamaServerBin   = 'D:\work\battlemage\llamacpp-win-vulkan\llama-server.exe',
    [string]$ModelsRoot       = 'D:\work\battlemage\models',
    [string]$SnapshotPath     = 'D:\work\b70tools\eval\snapshots\snapshot-truncated-70k.md',
    [string]$EvalScript       = 'D:\work\b70tools\eval\scripts\run-eval.ps1',
    [string]$B70RunsRoot      = 'D:\work\b70tools\runs',
    [string]$PythonBin        = 'D:\work\discoverlay\.venv\Scripts\python.exe',
    [string]$ComposerScript   = 'D:\work\discoverlay\composer\composer.py',
    [string]$ComposerWorkDir  = 'D:\work\discoverlay',
    [string]$WatchHostScript  = 'D:\work\b70tools\scripts\wow-impact\Watch-HostPressure.ps1',
    [string]$HudBin           = 'D:\work\discoverlay\build\Release\hud.exe',
    # Comma-separated list of model keys to run; default = all, in order.
    [string]$Models           = '',
    # Seconds to wait between models for VRAM/RAM to settle.
    [int]$BetweenModelsSec    = 25,
    # Rounds and prompts per model (keep equal to prior runs for comparison).
    [int]$RoundCount          = 3,
    [int]$PromptCount         = 5,
    [switch]$SkipHud,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Model matrix  -- ordered small to large (GB on disk).
# Each model runs the full 3-round x 5-prompt eval against snapshot-truncated-70k.md
# MaxTokens is tuned so generation time is meaningful but not interminable.
# ---------------------------------------------------------------------------
$AllModels = [ordered]@{
    'qwen2.5-14b-q4' = @{
        Label      = 'Qwen2.5 14B Q4_K_M'
        File       = 'qwen2.5-14b-instruct-q4_K_M.gguf'
        MaxTokens  = 2000
        Ctx        = 32768
        Note       = '8.4 GB  -- single-card territory, speed baseline'
    }
    'mistral-24b-q4' = @{
        Label      = 'Mistral-Small 24B Q4_K_M'
        File       = 'Mistral-Small-3.2-24B-Instruct-2506-Q4_K_M.gguf'
        MaxTokens  = 1500
        Ctx        = 32768
        Note       = '13.3 GB  -- new model, first stress run'
    }
    'qwen3-30b-moe-q4' = @{
        Label      = 'Qwen3 30B MoE Q4_K_M'
        File       = 'Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf'
        MaxTokens  = 1500
        Ctx        = 32768
        Note       = '17.3 GB  -- sparse MoE, previously milestone run'
    }
    'qwen2.5-32b-q4' = @{
        Label      = 'Qwen2.5 32B Q4_K_M'
        File       = 'qwen2.5-32b-instruct-q4_K_M.gguf'
        MaxTokens  = 1500
        Ctx        = 32768
        Note       = '18.5 GB  -- dense 32B speed/quality baseline'
    }
    'qwen2.5-coder-32b-q5' = @{
        Label      = 'Qwen2.5 Coder 32B Q5_K_M'
        File       = 'qwen2.5-coder-32b-instruct-q5_k_m.gguf'
        MaxTokens  = 1500
        Ctx        = 32768
        Note       = '21.7 GB  -- code-tuned dense 32B, higher quant'
    }
    'qwen2.5-32b-q6' = @{
        Label      = 'Qwen2.5 32B Q6_K'
        File       = 'qwen2.5-32b-instruct-q6_K.gguf'
        MaxTokens  = 1500
        Ctx        = 32768
        Note       = '25 GB  -- highest-quality 32B quant, VRAM stress'
    }
    'llama3.3-70b-q4' = @{
        Label      = 'Llama 3.3 70B Q4_K_M'
        File       = 'Llama-3.3-70B-Instruct-Q4_K_M.gguf'
        MaxTokens  = 1000
        Ctx        = 32768
        Note       = '39.6 GB  -- full dual-card stress, longest TTFT'
    }
}

# ---------------------------------------------------------------------------
# Filter to requested subset
# ---------------------------------------------------------------------------
if ($Models) {
    $requested = @($Models -split ',' | ForEach-Object { $_.Trim() })
    $filtered = [ordered]@{}
    foreach ($key in $requested) {
        if ($AllModels.Contains($key)) {
            $filtered[$key] = $AllModels[$key]
        } else {
            Write-Warning "Unknown model key '$key'  -- skipping.  Valid keys: $($AllModels.Keys -join ', ')"
        }
    }
    $ActiveModels = $filtered
} else {
    $ActiveModels = $AllModels
}

if ($ActiveModels.Count -eq 0) { throw 'No models selected.' }

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
foreach ($p in @($B70Tools, $LlamaServerBin, $EvalScript, $SnapshotPath,
                  $PythonBin, $ComposerScript, $WatchHostScript)) {
    if (-not (Test-Path $p)) { throw "Required file not found: $p" }
}
if (-not $SkipHud -and -not (Test-Path $HudBin)) { throw "hud.exe not found: $HudBin" }

# ---------------------------------------------------------------------------
# Print plan
# ---------------------------------------------------------------------------
Write-Output ''
Write-Output '===================================================================='
Write-Output '  STRESS SUITE  -- Model Load-Test Sweep'
Write-Output '===================================================================='
Write-Output "  Snapshot  : $SnapshotPath"
Write-Output "  Rounds    : $RoundCount x $PromptCount prompts each"
Write-Output "  Models    : $($ActiveModels.Count)"
$idx = 1
foreach ($key in $ActiveModels.Keys) {
    $m = $ActiveModels[$key]
    Write-Output ("  " + $idx.ToString().PadLeft(2) + ". $($m.Label.PadRight(35)) $($m.Note)")
    $idx++
}
Write-Output '===================================================================='
Write-Output ''

if ($DryRun) {
    Write-Output '[dry-run] Exiting without starting anything.'
    return
}

# ---------------------------------------------------------------------------
# Create suite-level run dir (b70tools writes here; composer tails this)
# ---------------------------------------------------------------------------
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$SuiteRunDir = Join-Path $B70RunsRoot ("stress-suite-$stamp")
New-Item -ItemType Directory -Force -Path $SuiteRunDir | Out-Null

$FeedPath      = Join-Path $SuiteRunDir 'stress-feed.jsonl'
$HostPressPath = Join-Path $SuiteRunDir 'host-pressure.jsonl'
$OrchestratorLog = Join-Path $SuiteRunDir 'orchestrator.log'

# Touch the feed file so the composer tail doesn't wait.
[System.IO.File]::WriteAllText($FeedPath, '', [System.Text.Encoding]::UTF8)

Write-Output "[suite] run dir   : $SuiteRunDir"
Write-Output "[suite] feed file : $FeedPath"
Write-Output ''

# ---------------------------------------------------------------------------
# Feed write helper
# ---------------------------------------------------------------------------
function Add-FeedLine {
    param([string]$Text, [string]$State = 'info')
    $record = (@{ text = $Text; state = $State } | ConvertTo-Json -Compress)
    # AppendAllText avoids the BOM that Add-Content -Encoding UTF8 can prepend on PS 5.1.
    [System.IO.File]::AppendAllText($FeedPath, $record + "`n", [System.Text.Encoding]::UTF8)
}

# ---------------------------------------------------------------------------
# Log helper (writes to orchestrator log + stdout)
# ---------------------------------------------------------------------------
function Write-Log {
    param([string]$Msg)
    $ts = (Get-Date -Format 'HH:mm:ss')
    $line = "[$ts] $Msg"
    Write-Output $line
    Add-Content -LiteralPath $OrchestratorLog -Value $line -Encoding UTF8
}

# ---------------------------------------------------------------------------
# Background processes to clean up on exit
# ---------------------------------------------------------------------------
$Cleanups = [System.Collections.Generic.List[System.Diagnostics.Process]]::new()

function Stop-Cleanups {
    foreach ($p in $Cleanups) {
        if ($p -and -not $p.HasExited) {
            try { $p | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
        }
    }
}

try {
    # -----------------------------------------------------------------------
    # 1. b70tools  -- continuous GPU telemetry for the overlay
    # -----------------------------------------------------------------------
    Write-Log '[telemetry] starting b70tools (continuous)...'
    $b70Proc = Start-Process -FilePath $B70Tools `
        -ArgumentList @('run', '--ticks', '0', '--out', $SuiteRunDir) `
        -PassThru -WindowStyle Hidden
    $Cleanups.Add($b70Proc)
    Write-Log "[telemetry] b70tools PID=$($b70Proc.Id)"
    Start-Sleep -Seconds 3   # let it write the first few events

    # -----------------------------------------------------------------------
    # 2. Watch-HostPressure sidecar
    # -----------------------------------------------------------------------
    Write-Log '[host] starting Watch-HostPressure...'
    $hostArgs = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', $WatchHostScript,
        '-OutPath', $HostPressPath,
        '-IntervalSec', '2',
        '-ProcessNames', 'llama-server,llama-cli,b70tools'
    )
    $hostProc = Start-Process -FilePath 'powershell.exe' -ArgumentList $hostArgs `
        -PassThru -WindowStyle Hidden
    $Cleanups.Add($hostProc)
    Write-Log "[host] Watch-HostPressure PID=$($hostProc.Id)"

    # -----------------------------------------------------------------------
    # 3. hud.exe  -- the overlay renderer
    # -----------------------------------------------------------------------
    if (-not $SkipHud) {
        Write-Log '[hud] starting hud.exe...'
        $hudProc = Start-Process -FilePath $HudBin -PassThru -WindowStyle Normal
        $Cleanups.Add($hudProc)
        Write-Log "[hud] hud.exe PID=$($hudProc.Id)"
        Start-Sleep -Seconds 1
    }

    # -----------------------------------------------------------------------
    # 4. discoverlay composer  -- stress mode
    # -----------------------------------------------------------------------
    Write-Log '[composer] starting composer --stress-mode...'
    $composerArgs = @(
        $ComposerScript,
        '--run',          $SuiteRunDir,
        '--combat-feed',  $FeedPath,
        '--stress-mode',
        '--cadence-ms',   '1000'
    )
    $composerProc = Start-Process -FilePath $PythonBin -ArgumentList $composerArgs `
        -WorkingDirectory $ComposerWorkDir `
        -PassThru -WindowStyle Hidden
    $Cleanups.Add($composerProc)
    Write-Log "[composer] composer PID=$($composerProc.Id)"
    Start-Sleep -Seconds 2   # let it write the first overlay spec

    # -----------------------------------------------------------------------
    # Initial feed banner
    # -----------------------------------------------------------------------
    Add-FeedLine "STRESS SUITE  -- $($ActiveModels.Count) models" 'info'
    Add-FeedLine "Snapshot: $(Split-Path $SnapshotPath -Leaf)" 'info'
    Add-FeedLine "Started: $(Get-Date -Format 'HH:mm')" 'info'
    Add-FeedLine '----------------------------------------' 'stale'

    # -----------------------------------------------------------------------
    # 5. Model sweep
    # -----------------------------------------------------------------------
    $suiteStart = Get-Date
    $modelIndex = 0
    $ResultTable = [System.Collections.Generic.List[pscustomobject]]::new()

    foreach ($key in $ActiveModels.Keys) {
        $modelIndex++
        $model = $ActiveModels[$key]
        $modelPath = Join-Path $ModelsRoot $model.File

        if (-not (Test-Path $modelPath)) {
            Write-Log "SKIP: model file not found: $modelPath"
            $skipMsg = "$modelIndex SKIP: $($model.Label) -- file missing"
            Add-FeedLine $skipMsg 'warn'
            continue
        }

        Write-Log ''
        Write-Log "=== MODEL $modelIndex/$($ActiveModels.Count): $($model.Label) ==="
        Write-Log "    $($model.Note)"

        Add-FeedLine '' 'info'
        $modelBanner = "$modelIndex/$($ActiveModels.Count): $($model.Label)"
        Add-FeedLine $modelBanner 'info'
        Add-FeedLine "    $($model.Note)" 'stale'

        $modelStart = Get-Date
        $configName = "$key-stress-$stamp"

        # Eval args (matches run-eval.ps1 parameter set)
        $evalArgs = @(
            '-ConfigName',      $configName,
            '-ModelPath',       $modelPath,
            '-SnapshotPath',    $SnapshotPath,
            '-Ctx',             "$($model.Ctx)",
            '-MaxTokens',       "$($model.MaxTokens)",
            '-RoundCount',      "$RoundCount",
            '-PromptCount',     "$PromptCount",
            '-LlamaServerBin',  $LlamaServerBin,
            '-B70Tools',        $B70Tools
        )

        $evalExitCode = 0

        try {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $EvalScript @evalArgs |
                ForEach-Object {
                    $line = [string]$_
                    Write-Output "    $line"
                    Add-Content -LiteralPath $OrchestratorLog -Value "    $line" -Encoding UTF8

                    # Parse key progress lines and forward to the feed.
                    if ($line -match '^\[preflight\] host RAM used = ([\d.]+) GB') {
                        Add-FeedLine "  RAM pre: $($Matches[1]) GB used" 'info'
                    } elseif ($line -match '^\[server\] starting llama-server') {
                        Add-FeedLine '  loading model...' 'warn'
                    } elseif ($line -match '^\[server\] /health: OK') {
                        Add-FeedLine '  server healthy' 'ok'
                    } elseif ($line -match '^\[warmup\] (\d+) tokens in (\d+) ms') {
                        $tok = [int]$Matches[1]; $ms = [int]$Matches[2]
                        $tps = if ($ms -gt 0) { [math]::Round($tok * 1000.0 / $ms, 1) } else { '?' }
                        Add-FeedLine "  warmup: $tok tok / ${ms}ms = $tps t/s" 'ok'
                    } elseif ($line -match '^\s+\[r(\d+) / p(\d+)\] (.+)') {
                        $r = $Matches[1]; $pnum = $Matches[2]
                        $title = $Matches[3].Substring(0, [math]::Min(28, $Matches[3].Length))
                        Add-FeedLine "  r$r/p$pnum $title" 'info'
                    } elseif ($line -match '^\s+round (\d+) wall: (\d+) ms') {
                        $r = $Matches[1]; $wallSec = [math]::Round([int]$Matches[2] / 1000.0, 1)
                        Add-FeedLine "  round $r done: ${wallSec}s" 'ok'
                    } elseif ($line -match '^\[gate\] .*status=(\S+)') {
                        $status = $Matches[1]
                        $gState = if ($status -eq 'healthy') { 'ok' } else { 'warn' }
                        Add-FeedLine "  gate: $status" $gState
                    } elseif ($line -match 'preflight gate failed|GATE FAILED') {
                        Add-FeedLine '  GATE FAILED  -- aborting model' 'crit'
                    } elseif ($line -match 'preflight: host RAM used .* exceeds') {
                        Add-FeedLine '  RAM unsafe  -- skipping model' 'crit'
                    }
                }
            $evalExitCode = $LASTEXITCODE
        } catch {
            $evalExitCode = 1
            Write-Log "[ERROR] model $($model.Label): $_"
            Add-FeedLine "  ERROR: $($_.Exception.Message.Substring(0, [math]::Min(60, $_.Exception.Message.Length)))" 'crit'
        }

        $modelWallSec = [math]::Round(((Get-Date) - $modelStart).TotalSeconds, 0)

        # Read back stats from the manifest that run-eval.ps1 wrote.
        $evalRunsRoot = Split-Path (Split-Path $EvalScript -Parent) -Parent
        $evalRunsDir  = Join-Path $evalRunsRoot 'runs'
        $latestRun = Get-ChildItem $evalRunsDir -Filter "$configName*" -Directory -ErrorAction SilentlyContinue |
                     Sort-Object LastWriteTime -Descending | Select-Object -First 1

        $avgTps = $null
        $p50TtftMs = $null

        if ($latestRun) {
            $manifestFile = Join-Path $latestRun.FullName 'manifest.json'
            if (Test-Path $manifestFile) {
                try {
                    $mf = Get-Content $manifestFile -Raw -Encoding UTF8 | ConvertFrom-Json
                    $tpsList = [System.Collections.Generic.List[double]]::new()
                    $ttftList = [System.Collections.Generic.List[double]]::new()
                    foreach ($rnd in $mf.rounds.PSObject.Properties) {
                        foreach ($prm in $rnd.Value.prompts.PSObject.Properties) {
                            $ti = $prm.Value.timings
                            if ($ti -and $ti.predicted_per_second) {
                                $tpsList.Add([double]$ti.predicted_per_second)
                            }
                            if ($ti -and $ti.prompt_ms -and $ti.prompt_n -gt 0) {
                                $ttftList.Add([double]$ti.prompt_ms)
                            }
                        }
                    }
                    if ($tpsList.Count -gt 0) {
                        $avgTps = [math]::Round(($tpsList | Measure-Object -Average).Average, 1)
                    }
                    if ($ttftList.Count -gt 0) {
                        $sorted = $ttftList | Sort-Object
                        $mid = [int][math]::Floor($sorted.Count / 2)
                        $p50TtftMs = [math]::Round($sorted[$mid] / 1000.0, 1)
                    }
                } catch {
                    Write-Log "[warn] failed to read manifest for $configName : $_"
                }
            }
        }

        # Write per-model summary to feed
        if ($evalExitCode -eq 0) {
            $summaryParts = @("  -> exit=ok  wall=${modelWallSec}s")
            if ($avgTps)     { $summaryParts += " | $avgTps t/s" }
            if ($p50TtftMs)  { $summaryParts += " | p50-TTFT ${p50TtftMs}s" }
            Add-FeedLine ($summaryParts -join '') 'ok'
        } else {
            Add-FeedLine "  -> exit=$evalExitCode  wall=${modelWallSec}s  -- FAILED" 'crit'
        }

        $ResultTable.Add([pscustomobject]@{
            Key      = $key
            Label    = $model.Label
            ExitCode = $evalExitCode
            WallSec  = $modelWallSec
            AvgTps   = $avgTps
            P50TtftS = $p50TtftMs
        })

        Add-FeedLine '----------------------------------------' 'stale'

        # Settle between models so VRAM + host RAM fully release.
        if ($modelIndex -lt $ActiveModels.Count) {
            Write-Log "[suite] waiting ${BetweenModelsSec}s for VRAM/RAM to settle..."
            Add-FeedLine "  [settling ${BetweenModelsSec}s...]" 'stale'
            Start-Sleep -Seconds $BetweenModelsSec
        }
    }

    # -----------------------------------------------------------------------
    # Suite summary
    # -----------------------------------------------------------------------
    $suiteSec = [math]::Round(((Get-Date) - $suiteStart).TotalSeconds, 0)
    $suiteMin  = [math]::Round($suiteSec / 60.0, 1)

    Add-FeedLine '' 'info'
    Add-FeedLine "SUITE COMPLETE  -- ${suiteMin}m total" 'ok'
    Add-FeedLine '' 'info'

    Write-Output ''
    Write-Output '===================================================================='
    Write-Output "  SUITE COMPLETE  -- ${suiteMin}m total"
    Write-Output '===================================================================='
    Write-Output ('  {0,-30} {1,8} {2,8} {3,10}' -f 'Model', 'Wall(s)', 'Avg t/s', 'p50-TTFT')
    Write-Output ('  ' + ('-' * 60))
    foreach ($r in $ResultTable) {
        $label = $r.Label.Substring(0, [math]::Min(28, $r.Label.Length))
        $tps  = if ($null -ne $r.AvgTps)   { $r.AvgTps.ToString('F1') } else { '-' }
        $ttft = if ($null -ne $r.P50TtftS) { ($r.P50TtftS.ToString('F1') + 's') } else { '-' }
        $exit = if ($r.ExitCode -eq 0) { 'ok' } else { "exit=$($r.ExitCode)" }
        Write-Output ('  {0,-30} {1,8} {2,8} {3,10}  {4}' -f $label, $r.WallSec, $tps, $ttft, $exit)
        $summaryLine = "$label  $tps t/s  $ttft TTFT  $exit"
        Add-FeedLine $summaryLine 'info'
    }
    Write-Output '===================================================================='
    Write-Output ''
    Write-Output "results in : $SuiteRunDir"
    Write-Output "eval runs  : $evalRunsDir"

    # Write machine-readable summary next to the feed
    $summaryPath = Join-Path $SuiteRunDir 'suite-summary.json'
    @{
        suite_run_dir = $SuiteRunDir
        snapshot      = $SnapshotPath
        rounds        = $RoundCount
        prompts       = $PromptCount
        started_at    = $suiteStart.ToString('o')
        finished_at   = (Get-Date).ToString('o')
        wall_sec      = $suiteSec
        results       = @($ResultTable)
    } | ConvertTo-Json -Depth 8 | Out-File -FilePath $summaryPath -Encoding UTF8 -Force
    Write-Output "[suite] summary   : $summaryPath"

    Add-FeedLine "Results: $summaryPath" 'stale'

} finally {
    Write-Log '[suite] stopping background processes...'
    Stop-Cleanups
    Write-Log '[suite] done.'
}
