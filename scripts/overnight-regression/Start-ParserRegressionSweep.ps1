# Start-ParserRegressionSweep.ps1
#
# Phase 1 of the 2026-06-16 overnight plan (docs/overnight-regression-plan-2026-06-16.md).
# CPU-only, zero GPU risk -- safe to run unattended before the inference sweep.
#
# It:
#   1. Builds Tempo.sln (Release) so parse wall-times are perf-meaningful.
#   2. Runs the regression test suite (Parity + Percolation + ReactionRate) via dotnet test.
#   3. For each new combat log + canon fixture:
#        --memory-bench   -> parse wall, ParseResult heap delta, % of 2.2 GB budget,
#                            session/encounter/pull counts, INV-PX1.5 branch verdict
#        --export-evidence -> the LLM evidence-markdown prefix, SHA-256-keyed disk cache
#                            (re-runs are loads, not re-parses); determinism double-export
#                            on a representative log (prefix-cache-safety check)
#   4. Writes results.json (no BOM) + summary.md into a timestamped run dir.
#
# Usage:
#   .\Start-ParserRegressionSweep.ps1
#   .\Start-ParserRegressionSweep.ps1 -DryRun
#   .\Start-ParserRegressionSweep.ps1 -SkipTests
#   .\Start-ParserRegressionSweep.ps1 -SinceDate 2026-06-01 -SkipBuild

param(
    [string]   $TempoRoot   = 'D:\World of Warcraft\Tempo',
    [string]   $LeopardRoot = 'D:\work\leopard',
    [string]   $LogsDir     = 'D:\World of Warcraft\_retail_\Logs',
    [string]   $OutRoot     = 'D:\work\b70tools\runs',
    [string]   $CacheRoot   = 'D:\work\b70tools\runs\_evidence-cache',
    # Combat logs with LastWriteTime >= this date are swept. Default = post round-1.
    [datetime] $SinceDate   = '2026-06-01',
    # Canon fixtures always included (curated, stable inputs).
    [string[]] $CanonFixtures = @(
        'D:\World of Warcraft\Tempo\fixture-canon\fixtures\2026-05-27-m+-mixed-keys\WoWCombatLog-2026-05-27-m+-mixed-keys.txt',
        'D:\World of Warcraft\Tempo\docs\liveplay-2026-05-26\WoWCombatLog-pit-plus11.txt'
    ),
    [double]   $MemBudgetGB = 2.2,
    [switch]   $SkipBuild,
    [switch]   $SkipTests,
    [switch]   $DryRun
)

$ErrorActionPreference = 'Stop'
$sw = [System.Diagnostics.Stopwatch]::StartNew()

# --- helpers ---------------------------------------------------------------
function Write-JsonNoBom([object]$Obj, [string]$Path) {
    $json = $Obj | ConvertTo-Json -Depth 12
    [System.IO.File]::WriteAllText($Path, $json, (New-Object System.Text.UTF8Encoding($false)))
}
function Write-TextNoBom([string]$Text, [string]$Path) {
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}
function Match1([string]$Text, [string]$Pattern) {
    $m = [regex]::Match($Text, $Pattern)
    if ($m.Success) { return $m.Groups[1].Value } else { return $null }
}

$diagProj = Join-Path $TempoRoot 'src\Tempo.Diagnostics'
$sln      = Join-Path $TempoRoot 'src\Tempo.sln'
$diagExe  = Join-Path $TempoRoot 'src\Tempo.Diagnostics\bin\Release\net9.0\Tempo.Diagnostics.exe'

# --- resolve the corpus ----------------------------------------------------
$newLogs = @()
if (Test-Path $LogsDir) {
    $newLogs = Get-ChildItem -Path $LogsDir -Filter 'WoWCombatLog*.txt' -File |
        Where-Object { $_.LastWriteTime -ge $SinceDate } |
        Sort-Object LastWriteTime |
        Select-Object -ExpandProperty FullName
}
$corpus = @()
$corpus += $newLogs
foreach ($f in $CanonFixtures) { if (Test-Path $f) { $corpus += $f } }
$corpus = $corpus | Select-Object -Unique

if ($corpus.Count -eq 0) { Write-Error "No combat logs found (LogsDir=$LogsDir, SinceDate=$SinceDate)."; exit 2 }

# Determinism double-export target: the smallest log >= 30 MB (cheap but non-trivial).
$detTarget = $corpus |
    ForEach-Object { Get-Item $_ } |
    Where-Object { $_.Length -ge 30MB } |
    Sort-Object Length |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $detTarget) { $detTarget = $corpus[0] }

Write-Host "=== Parser regression sweep plan ===" -ForegroundColor Cyan
Write-Host "Tempo root   : $TempoRoot"
Write-Host "Corpus       : $($corpus.Count) logs"
$corpus | ForEach-Object {
    $fi = Get-Item $_
    Write-Host ("  {0,8:N0} MB  {1}" -f ($fi.Length/1MB), $fi.Name)
}
Write-Host "Determinism  : $([System.IO.Path]::GetFileName($detTarget))"
Write-Host "Mem budget   : $MemBudgetGB GB"
Write-Host "Build/Tests  : build=$(-not $SkipBuild) tests=$(-not $SkipTests)"

if ($DryRun) { Write-Host "`n[DryRun] Nothing executed." -ForegroundColor Yellow; exit 0 }

# --- run dir ---------------------------------------------------------------
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $OutRoot "parser-regression-$stamp"
New-Item -ItemType Directory -Force -Path $runDir   | Out-Null
New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
$evidenceDir = Join-Path $runDir 'evidence'
New-Item -ItemType Directory -Force -Path $evidenceDir | Out-Null
Write-Host "`nRun dir: $runDir" -ForegroundColor Green

$tempoCommit = (& git -C $TempoRoot rev-parse --short HEAD 2>$null)
$b70Commit   = (& git -C 'D:\work\b70tools' rev-parse --short HEAD 2>$null)

# --- build -----------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`n--- dotnet build (Release) ---" -ForegroundColor Cyan
    & dotnet build $sln -c Release -v minimal | Tee-Object -FilePath (Join-Path $runDir 'build.log')
    if ($LASTEXITCODE -ne 0) { Write-Error "Build failed (exit $LASTEXITCODE). See build.log."; exit 3 }
}
if (-not (Test-Path $diagExe)) { Write-Error "Diagnostics exe not found: $diagExe"; exit 3 }

# --- tests (Phase 1A: all three regression suites) -------------------------
# Tempo (Parity + Percolation/ReactionRate) + leopard-host (RaidUI math ports, 62 xUnit)
# + leopard-web (vitest). Each suite is independent; a failure is recorded, not fatal.
$suites = @()
if (-not $SkipTests) {
    $leopardHostTests = Join-Path $LeopardRoot 'src\leopard-host.Tests'
    $leopardWeb       = Join-Path $LeopardRoot 'src\leopard-web'
    $suiteDefs = @(
        @{ name = 'tempo';        kind = 'dotnet'; path = $sln },
        @{ name = 'leopard-host'; kind = 'dotnet'; path = $leopardHostTests },
        @{ name = 'leopard-web';  kind = 'vitest'; path = $leopardWeb }
    )
    foreach ($s in $suiteDefs) {
        Write-Host "`n--- tests: $($s.name) ($($s.kind)) ---" -ForegroundColor Cyan
        if (-not (Test-Path $s.path)) {
            Write-Host "  SKIP (path not found: $($s.path))" -ForegroundColor Yellow
            $suites += [ordered]@{ name = $s.name; ran = $false; exitCode = $null; durationSec = $null; note = 'path-not-found' }
            continue
        }
        $tsw = [System.Diagnostics.Stopwatch]::StartNew()
        $log = Join-Path $runDir "test-$($s.name).log"
        if ($s.kind -eq 'dotnet') {
            & dotnet test $s.path -c Release `
                --logger "trx;LogFileName=$($s.name).trx" `
                --results-directory (Join-Path $runDir 'test-results') `
                -v minimal *>&1 | Tee-Object -FilePath $log
        } else {
            Push-Location $s.path
            & npm test *>&1 | Tee-Object -FilePath $log
            Pop-Location
        }
        $ec = $LASTEXITCODE
        $tsw.Stop()
        $dur = [math]::Round($tsw.Elapsed.TotalSeconds, 1)
        $suites += [ordered]@{ name = $s.name; ran = $true; exitCode = $ec; durationSec = $dur; note = $null }
        if ($ec -ne 0) { Write-Host "  $($s.name): FAILED (exit $ec, ${dur}s) -- continuing." -ForegroundColor Yellow }
        else { Write-Host "  $($s.name): PASSED (${dur}s)." -ForegroundColor Green }
    }
}
$testSummary = [ordered]@{ ran = ($suites.Count -gt 0); suites = $suites }

# --- per-log sweep ---------------------------------------------------------
$rows = @()
foreach ($log in $corpus) {
    $fi   = Get-Item $log
    $name = $fi.Name
    Write-Host "`n--- $name ($([math]::Round($fi.Length/1MB,0)) MB) ---" -ForegroundColor Cyan

    $sha = (Get-FileHash -Algorithm SHA256 -Path $log).Hash.ToLower()

    # memory-bench (one full parse + GC-measured heap delta)
    $mbOut = & $diagExe --log $log --memory-bench 2>&1 | Out-String
    $mbExit = $LASTEXITCODE
    Write-TextNoBom $mbOut (Join-Path $runDir "membench-$name.txt")

    $parseWall  = Match1 $mbOut 'parse wall\s*=\s*([\d.]+)\s*s'
    $deltaStr   = (Match1 $mbOut 'delta \(ParseResult cost\)\s*=\s*(.+)')
    $pctBudget  = Match1 $mbOut 'delta as % of budget\s*=\s*([\d.]+)%'
    $sessions   = Match1 $mbOut '(?m)^sessions\s*=\s*(\d+)'
    $encounters = Match1 $mbOut '(?m)^encounters\s*=\s*(\d+)'
    $pulls      = Match1 $mbOut '(?m)^pulls\s*=\s*(\d+)'
    $catEvents  = Match1 $mbOut 'categorical events held\s*=\s*([\d,]+)'
    $branch     = Match1 $mbOut 'verdict \(per decision rule\):\s*(BRANCH \([abc]\))'

    if ($deltaStr) { $deltaStr = $deltaStr.Trim() }
    $deltaGB = $null
    if ($pctBudget) { $deltaGB = [math]::Round(([double]$pctBudget / 100.0) * 2.2, 3) }

    # evidence export with SHA-keyed cache
    $cacheFile = Join-Path $CacheRoot "$sha.md"
    $evOut = Join-Path $evidenceDir "evidence-$name.md"
    $cacheHit = $false
    $evExit = 0
    if (Test-Path $cacheFile) {
        Copy-Item $cacheFile $evOut -Force
        $cacheHit = $true
        Write-Host "  evidence: CACHE HIT ($sha)" -ForegroundColor DarkGray
    } else {
        & $diagExe --log $log --export-evidence $evOut *> (Join-Path $runDir "export-$name.log")
        $evExit = $LASTEXITCODE
        if ($evExit -eq 0 -and (Test-Path $evOut)) { Copy-Item $evOut $cacheFile -Force }
    }
    $evBytes = if (Test-Path $evOut) { (Get-Item $evOut).Length } else { 0 }
    $evRaidEmpty = $false
    if ((Test-Path $evOut) -and (Select-String -Path $evOut -SimpleMatch 'no raid sessions found' -Quiet)) {
        $evRaidEmpty = $true
    }

    # determinism double-export (representative log only)
    $determinismOk = $null
    if ($log -eq $detTarget -and -not $cacheHit -and (Test-Path $evOut)) {
        $evOut2 = Join-Path $runDir "evidence-determinism-second.md"
        & $diagExe --log $log --export-evidence $evOut2 *> $null
        if (Test-Path $evOut2) {
            $h1 = (Get-FileHash -Algorithm SHA256 $evOut).Hash
            $h2 = (Get-FileHash -Algorithm SHA256 $evOut2).Hash
            $determinismOk = ($h1 -eq $h2)
            Write-Host "  determinism: $(if ($determinismOk) {'IDENTICAL'} else {'*** DIVERGED ***'})" -ForegroundColor $(if ($determinismOk) {'Green'} else {'Red'})
        }
    }

    # assertions
    $parseOk    = ($mbExit -eq 0)
    $hasContent = (($encounters -as [int]) -gt 0) -or (($pulls -as [int]) -gt 0)
    $memOk      = ($deltaGB -ne $null) -and ($deltaGB -lt $MemBudgetGB)

    $flags = @()
    if (-not $parseOk)    { $flags += 'PARSE-FAIL' }
    if (-not $hasContent) { $flags += 'NO-ENCOUNTERS' }
    if (-not $memOk)      { $flags += 'MEM-OVER-BUDGET' }
    if ($branch -eq 'BRANCH (c)') { $flags += 'INV-PX1.5-REQUIRED' }
    if ($determinismOk -eq $false) { $flags += 'NONDETERMINISTIC-EVIDENCE' }

    Write-Host ("  parse={0}s  heap={1} ({2}%)  enc={3} pulls={4}  branch={5}  {6}" -f `
        $parseWall, $deltaStr, $pctBudget, $encounters, $pulls, $branch, ($(if ($flags) {($flags -join ',')} else {'OK'})))

    $rows += [ordered]@{
        log               = $name
        path              = $log
        sizeMB            = [math]::Round($fi.Length/1MB, 1)
        sha256            = $sha
        parseExit         = $mbExit
        parseWallSec      = $parseWall
        heapDelta         = $deltaStr
        heapDeltaGB       = $deltaGB
        pctOfBudget       = $pctBudget
        sessions          = $sessions
        encounters        = $encounters
        pulls             = $pulls
        categoricalEvents = $catEvents
        invPx15Branch     = $branch
        evidenceBytes     = $evBytes
        evidenceCacheHit  = $cacheHit
        evidenceRaidEmpty = $evRaidEmpty
        determinismOk     = $determinismOk
        flags             = $flags
    }
}

$sw.Stop()

# --- results ---------------------------------------------------------------
$result = [ordered]@{
    runId        = "parser-regression-$stamp"
    generatedAt  = (Get-Date).ToString('o')
    tempoRoot    = $TempoRoot
    tempoCommit  = $tempoCommit
    b70Commit    = $b70Commit
    memBudgetGB  = $MemBudgetGB
    durationSec  = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    tests        = $testSummary
    logs         = $rows
}
Write-JsonNoBom $result (Join-Path $runDir 'results.json')

# --- summary.md ------------------------------------------------------------
# @(...) forces an array: a single matching [ordered] row would otherwise report its KEY
# count via .Count, not 1 (PowerShell hashtable-.Count gotcha).
$flagged = @($rows | Where-Object { $_.flags.Count -gt 0 })
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# Parser regression sweep -- $stamp")
[void]$sb.AppendLine()
[void]$sb.AppendLine("- Tempo commit: ``$tempoCommit`` | b70tools commit: ``$b70Commit``")
[void]$sb.AppendLine("- Duration: $($result.durationSec)s | Mem budget: $MemBudgetGB GB")
if ($testSummary.ran) {
    foreach ($s in $testSummary.suites) {
        if (-not $s.ran) { [void]$sb.AppendLine("- Tests ($($s.name)): skipped ($($s.note))"); continue }
        $tv = if ($s.exitCode -eq 0) { 'PASS' } else { "FAIL (exit $($s.exitCode))" }
        [void]$sb.AppendLine("- Tests ($($s.name)): **$tv** ($($s.durationSec)s)")
    }
} else {
    [void]$sb.AppendLine("- Regression tests: skipped")
}
[void]$sb.AppendLine("- Logs swept: $($rows.Count) | Flagged: $($flagged.Count)")
[void]$sb.AppendLine()
[void]$sb.AppendLine("| Log | MB | Parse s | Heap | %budget | Enc | Pulls | Branch | Cache | Flags |")
[void]$sb.AppendLine("|---|---:|---:|---|---:|---:|---:|---|:--:|---|")
foreach ($r in $rows) {
    $cache = if ($r.evidenceCacheHit) { 'hit' } else { 'miss' }
    $fl    = if ($r.flags.Count -gt 0) { ($r.flags -join ',') } else { 'OK' }
    [void]$sb.AppendLine("| $($r.log) | $($r.sizeMB) | $($r.parseWallSec) | $($r.heapDelta) | $($r.pctOfBudget) | $($r.encounters) | $($r.pulls) | $($r.invPx15Branch) | $cache | $fl |")
}
[void]$sb.AppendLine()
if ($flagged.Count -eq 0) {
    [void]$sb.AppendLine("**All logs green.** Evidence prefixes cached under ``$CacheRoot`` for Phase 2.")
} else {
    [void]$sb.AppendLine("**Review flagged logs above before trusting Phase 2 evidence.**")
}
Write-TextNoBom ($sb.ToString()) (Join-Path $runDir 'summary.md')

Write-Host "`n=== DONE ===" -ForegroundColor Green
Write-Host "results.json + summary.md -> $runDir"
if ($flagged.Count -gt 0) { Write-Host "FLAGGED: $($flagged.Count) log(s) need review." -ForegroundColor Yellow }
