param(
    [Parameter(Mandatory=$true)] [string]$Scenario,
    [int]$DurationSec = 300,
    [string]$RunRoot = 'D:\work\b70tools\runs',
    [string]$FixedRunDir = '',
    [string]$B70Tools = 'D:\work\b70tools\build\b70tools.exe',
    [string]$DisplayAdapterId = '',
    [string]$InferenceAdapterId = '',
    [string]$BindingIdentity = '',
    [string]$TempoRepo = 'C:\path\to\World of Warcraft\Tempo',
    [string]$WowLogPath = 'C:\path\to\World of Warcraft\_retail_\Logs\WoWCombatLog.txt',
    [string]$Model = '',
    [string]$LlamaBuild = '',
    [string]$GpuBinding = '',
    [string]$FrameCsvPath = '',
    [string]$PresentMonPath = '',
    [string[]]$PresentMonArgs = @(),
    [string[]]$HostProcessNames = @('Wow', 'WowT', 'WowClassic', 'llama-server', 'llama-cli', 'ollama', 'dotnet', 'Tempo.Host'),
    [switch]$TailWowLog,
    [double]$WowLogTailIntervalSec = 7.0,
    [switch]$SkipB70Analysis
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
. (Join-Path $scriptRoot 'NoBom.ps1')
$hostWatcher = Join-Path $scriptRoot 'Watch-HostPressure.ps1'
$logTailer = Join-Path $scriptRoot 'Watch-WowLogTail.ps1'

if (-not (Test-Path $B70Tools)) { throw "b70tools not found: $B70Tools" }
if (-not (Test-Path $hostWatcher)) { throw "host pressure watcher not found: $hostWatcher" }
if ($TailWowLog -and -not (Test-Path $logTailer)) { throw "WoW log tailer not found: $logTailer" }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$slug = ($Scenario.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
if (-not $slug) { $slug = 'scenario' }
$runDir = if ($FixedRunDir) { $FixedRunDir } else { Join-Path $RunRoot ("wow-impact-$stamp-$slug") }
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$eventsPath = Join-Path $runDir 'events.jsonl'
$hostPressurePath = Join-Path $runDir 'host-pressure.jsonl'
$wowLogTailPath = Join-Path $runDir 'wow-log-tail.jsonl'
$notesPath = Join-Path $runDir 'notes.md'

$b70Commit = ''
try { $b70Commit = (& git -C $repoRoot rev-parse --short HEAD 2>$null | Out-String).Trim() } catch {}
$tempoCommit = ''
if (Test-Path $TempoRepo) {
    try { $tempoCommit = (& git -C $TempoRepo rev-parse --short HEAD 2>$null | Out-String).Trim() } catch {}
}

$manifest = [ordered]@{
    run_id = Split-Path -Leaf $runDir
    scenario = $Scenario
    step = $null
    started_at = (Get-Date).ToString('o')
    duration_sec = $DurationSec
    run_dir = $runDir
    b70tools_bin = $B70Tools
    b70tools_commit = $b70Commit
    tempo_repo = $TempoRepo
    tempo_commit = $tempoCommit
    wow_log_path = $WowLogPath
    display_adapter_id = $DisplayAdapterId
    inference_adapter_id = $InferenceAdapterId
    model = $Model
    llama_build = $LlamaBuild
    binding_identity = $BindingIdentity
    frame_csv_path = $FrameCsvPath
    presentmon_path = $PresentMonPath
    presentmon_args = $PresentMonArgs
    host_process_names = $HostProcessNames
    artifacts = [ordered]@{
        events_jsonl = $eventsPath
        host_pressure_jsonl = $hostPressurePath
        wow_log_tail_jsonl = if ($TailWowLog) { $wowLogTailPath } else { $null }
        inference_requests_jsonl = (Join-Path $runDir 'inference-requests.jsonl')
        frame_times_csv = if ($FrameCsvPath) { $FrameCsvPath } else { (Join-Path $runDir 'frame-times.csv') }
    }
}

Write-JsonNoBom -Path (Join-Path $runDir 'manifest.json') -Value $manifest -Depth 10

Write-TextNoBom -Path $notesPath -Content @"
# $Scenario

Started: $($manifest.started_at)
Duration: $DurationSec seconds

Manual notes:

"@

Write-Output "[run] $runDir"
Write-Output "[host] starting pressure capture"

function Quote-ProcessArg {
    param([string]$Value)
    return '"' + ($Value -replace '"', '\"') + '"'
}

$hostArgs = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $hostWatcher,
    '-OutPath', $hostPressurePath,
    '-IntervalSec', '1',
    '-ProcessNames', ($HostProcessNames -join ',')
)
$hostProc = Start-Process -FilePath 'powershell.exe' -ArgumentList $hostArgs -PassThru -WindowStyle Hidden
$manifest.host_pressure_pid = $hostProc.Id

$logTailProc = $null
if ($TailWowLog) {
    if (-not (Test-Path $WowLogPath)) { throw "WoW combat log not found: $WowLogPath" }
    Write-Output "[wow-log] tailing $WowLogPath every $WowLogTailIntervalSec seconds"
    $logTailArgString = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', (Quote-ProcessArg $logTailer),
        '-LogPath', (Quote-ProcessArg $WowLogPath),
        '-OutPath', (Quote-ProcessArg $wowLogTailPath),
        '-IntervalSec', "$WowLogTailIntervalSec"
    ) -join ' '
    $logTailProc = Start-Process -FilePath 'powershell.exe' -ArgumentList $logTailArgString -PassThru -WindowStyle Hidden
    $manifest.wow_log_tail_pid = $logTailProc.Id
}

$presentProc = $null
if ($PresentMonPath) {
    if (-not (Test-Path $PresentMonPath)) { throw "PresentMon not found: $PresentMonPath" }
    Write-Output "[frame] starting PresentMon"
    $presentProc = Start-Process -FilePath $PresentMonPath -ArgumentList $PresentMonArgs -PassThru -WindowStyle Hidden
    $manifest.presentmon_pid = $presentProc.Id
}

Write-Output "[b70tools] starting telemetry for $DurationSec ticks"
$b70Proc = Start-Process -FilePath $B70Tools -ArgumentList @('run', '--ticks', "$DurationSec", '--out', $runDir) -PassThru -WindowStyle Hidden
$manifest.b70tools_pid = $b70Proc.Id
Write-JsonNoBom -Path (Join-Path $runDir 'manifest.json') -Value $manifest -Depth 10

try {
    $timeoutMs = [Math]::Max(60000, ($DurationSec + 90) * 1000)
    if (-not $b70Proc.WaitForExit($timeoutMs)) {
        throw "b70tools did not exit after timeout; telemetry process id $($b70Proc.Id)"
    }
    $manifest.b70tools_exit_code = $b70Proc.ExitCode
}
finally {
    if ($presentProc -and -not $presentProc.HasExited) {
        Write-Output "[frame] stopping PresentMon"
        try { $presentProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    if ($hostProc -and -not $hostProc.HasExited) {
        Write-Output "[host] stopping pressure capture"
        try { $hostProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    if ($logTailProc -and -not $logTailProc.HasExited) {
        Write-Output "[wow-log] stopping log tail"
        try { $logTailProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
}

Start-Sleep -Seconds 1

if (-not $SkipB70Analysis) {
    Write-Output "[b70tools] writing analysis artifacts"
    Write-TextNoBom -Path (Join-Path $runDir 'b70tools-adapters.txt') -Content ((& $B70Tools adapters $runDir 2>&1 | Out-String))
    Write-TextNoBom -Path (Join-Path $runDir 'b70tools-summary.txt') -Content ((& $B70Tools summarize $runDir 2>&1 | Out-String))
    Write-TextNoBom -Path (Join-Path $runDir 'b70tools-disagreements.txt') -Content ((& $B70Tools disagreements $runDir 2>&1 | Out-String))
    Write-TextNoBom -Path (Join-Path $runDir 'b70tools-self.txt') -Content ((& $B70Tools self $runDir 2>&1 | Out-String))
}

$manifest.finished_at = (Get-Date).ToString('o')
Write-JsonNoBom -Path (Join-Path $runDir 'manifest.json') -Value $manifest -Depth 10

Write-Output "[done] $runDir"
Write-Output "[next] Add inference events with Invoke-InferenceStimulus.ps1 during a run, then summarize with Summarize-WowImpactRun.ps1."
