# Run-OvernightAll.ps1
#
# One walk-away orchestrator for the 2026-06-16 overnight run. Runs each phase
# synchronously (each blocks until its process tree finishes), logs everything to a
# master run dir, and continues to the next phase even if one fails.
#
#   Phase 1A/1B  Start-ParserRegressionSweep.ps1   (3 test suites + Tempo parser sweep)
#   Phase 1C     Invoke-LeopardArtifactSweep.ps1   (RaidUI maths on real logs + cache regress)
#   Phase 2      Start-StressSuite.ps1 -SkipHud    (GPU multi-model throughput baseline)
#
# Usage:  & 'D:\work\b70tools\scripts\overnight-regression\Run-OvernightAll.ps1'

param(
    [string] $Root = 'D:\work\b70tools\scripts\overnight-regression',
    [switch] $SkipPhase2   # CPU-only regression run, no GPU sweep
)

$ErrorActionPreference = 'Continue'
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = "D:\work\b70tools\runs\overnight-$stamp"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$master = Join-Path $runDir 'overnight.log'

function Log([string]$m) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m
    Write-Host $line
    Add-Content -Path $master -Value $line -Encoding utf8
}

function Run-Phase([string]$name, [scriptblock]$action) {
    Log "=== START $name ==="
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $ec = 0
    try { & $action } catch { Log "!! $name threw: $($_.Exception.Message)"; $ec = 1 }
    $sw.Stop()
    if ($LASTEXITCODE) { $ec = $LASTEXITCODE }
    Log ("=== END   $name  (exit {0}, {1:N0}s) ===" -f $ec, $sw.Elapsed.TotalSeconds)
    return $ec
}

Log "overnight run -> $runDir"

$r1 = Run-Phase 'Phase1AB parser-regression' {
    & (Join-Path $Root 'Start-ParserRegressionSweep.ps1') *>&1 |
        Tee-Object -FilePath (Join-Path $runDir 'phase1ab.log')
}

$r1c = Run-Phase 'Phase1C leopard-artifact' {
    & (Join-Path $Root 'Invoke-LeopardArtifactSweep.ps1') *>&1 |
        Tee-Object -FilePath (Join-Path $runDir 'phase1c.log')
}

$r2 = 'skipped'
if (-not $SkipPhase2) {
    $r2 = Run-Phase 'Phase2 stress-suite' {
        & 'D:\work\b70tools\scripts\stress-suite\Start-StressSuite.ps1' -SkipHud *>&1 |
            Tee-Object -FilePath (Join-Path $runDir 'phase2.log')
    }
}

Log "ALL DONE.  phase1ab=$r1  phase1c=$r1c  phase2=$r2"
Log "artifacts under $runDir (+ per-phase run dirs in D:\work\b70tools\runs and eval\runs)"
