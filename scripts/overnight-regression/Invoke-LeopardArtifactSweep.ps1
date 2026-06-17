# Invoke-LeopardArtifactSweep.ps1
#
# Phase 1C of the 2026-06-16 overnight plan (docs/overnight-regression-plan-2026-06-16.md).
# CPU-only, zero GPU risk. Exercises the RaidUI math corpus ported into leopard-host
# (ADR-0005) on the real new combat-log corpus, and regression-tests the per-night JSON
# disk cache (determinism, mtime-skip, schema conformance).
#
# It:
#   1. Builds leopard-host (Release) and launches it --headless on :5280.
#   2. POST /api/parse for each new log -> one parse -> 12 cached artifacts under
#      %LOCALAPPDATA%\Leopard\cache\{name}__{mtimeTicks}.{kind}.
#   3. Asserts all 12 artifacts land per log (the 9 ported math modules ran clean).
#   4. Determinism: delete one log's cache, re-derive, diff artifacts byte-for-byte.
#   5. Cache-skip: re-POST and confirm no rewrite (mtime hit, files untouched).
#   6. Schema: assert known keys exist in signals.v1 / players.v1 / classify.v1.
#   7. Writes leopard-results.json (no BOM) + leopard-summary.md to the run dir.
#
# Usage:
#   .\Invoke-LeopardArtifactSweep.ps1
#   .\Invoke-LeopardArtifactSweep.ps1 -DryRun
#   .\Invoke-LeopardArtifactSweep.ps1 -SinceDate 2026-06-05 -SkipBuild

param(
    [string]   $LeopardRoot = 'D:\work\leopard',
    [string]   $LogsDir     = 'C:\path\to\World of Warcraft\_retail_\Logs',
    [string]   $OutRoot     = 'D:\work\b70tools\runs',
    [datetime] $SinceDate   = '2026-06-01',
    [int]      $Port        = 5280,
    [switch]   $SkipBuild,
    [switch]   $DryRun
)

$ErrorActionPreference = 'Stop'
$sw = [System.Diagnostics.Stopwatch]::StartNew()

# The 12 artifact suffixes Program.cs writes per parse (keyed by {name}__{mtimeTicks}).
$artifactKinds = @(
    'md', 'night.v1.json', 'trends.v2.json', 'trace.json', 'career.json', 'shape.v1.json',
    'signals.v1.json', 'affinity.v1.json', 'players.v1.json', 'coverage.v1.json',
    'segments.v1.json', 'classify.v1.json'
)
$cacheDir = Join-Path $env:LOCALAPPDATA 'Leopard\cache'
$hostProj = Join-Path $LeopardRoot 'src\leopard-host'
$base     = "http://localhost:$Port"

function Write-JsonNoBom([object]$Obj, [string]$Path) {
    [System.IO.File]::WriteAllText($Path, ($Obj | ConvertTo-Json -Depth 12), (New-Object System.Text.UTF8Encoding($false)))
}
function Write-TextNoBom([string]$Text, [string]$Path) {
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}
# Cache path for one log + kind, matching Program.cs: invalid filename chars -> '_'.
function Cache-Path([string]$logName, [datetime]$mtimeUtc, [string]$kind) {
    $invalid = [System.IO.Path]::GetInvalidFileNameChars()
    $safe = ($logName.ToCharArray() | ForEach-Object { if ($invalid -contains $_) { '_' } else { $_ } }) -join ''
    return Join-Path $cacheDir "$($safe)__$($mtimeUtc.Ticks).$kind"
}

# --- resolve corpus --------------------------------------------------------
if (-not (Test-Path $LogsDir)) { Write-Error "LogsDir not found: $LogsDir"; exit 2 }
$logs = Get-ChildItem -Path $LogsDir -Filter 'WoWCombatLog*.txt' -File |
    Where-Object { $_.LastWriteTime -ge $SinceDate } |
    Sort-Object LastWriteTime
if ($logs.Count -eq 0) { Write-Error "No logs >= $SinceDate under $LogsDir"; exit 2 }

# Determinism target: smallest log >= 30 MB (cheap re-derive, non-trivial content).
$detTarget = $logs | Where-Object { $_.Length -ge 30MB } | Sort-Object Length | Select-Object -First 1
if (-not $detTarget) { $detTarget = $logs[0] }

Write-Host "=== Leopard artifact + cache sweep plan ===" -ForegroundColor Cyan
Write-Host "Leopard host : $hostProj"
Write-Host "Cache dir    : $cacheDir"
Write-Host "Logs         : $($logs.Count) (>= $($SinceDate.ToString('yyyy-MM-dd')))"
Write-Host "Determinism  : $($detTarget.Name) ($([math]::Round($detTarget.Length/1MB,0)) MB)"
Write-Host "Artifacts/log: $($artifactKinds.Count)"
if ($DryRun) { Write-Host "`n[DryRun] Nothing executed." -ForegroundColor Yellow; exit 0 }

# --- run dir ---------------------------------------------------------------
$stamp  = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $OutRoot "leopard-artifact-$stamp"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
Write-Host "`nRun dir: $runDir" -ForegroundColor Green

# --- build -----------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`n--- dotnet build leopard-host (Release) ---" -ForegroundColor Cyan
    & dotnet build $hostProj -c Release -v minimal | Tee-Object -FilePath (Join-Path $runDir 'leopard-build.log')
    if ($LASTEXITCODE -ne 0) { Write-Error "leopard-host build failed (exit $LASTEXITCODE)."; exit 3 }
}
# Pick the exe matching the csproj TFM (net9.0-windows = the real WebView2 host with the
# 12-artifact pipeline), newest first. A stale net9.0\ single-target exe from before the
# math-port marathon only writes the boxscore .md -- never let -Recurse grab it by name order.
$hostExe = Get-ChildItem -Path (Join-Path $hostProj 'bin\Release') -Filter 'leopard-host.exe' -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -like '*net9.0-windows*' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $hostExe) { Write-Error "net9.0-windows leopard-host.exe not found under bin\Release (build first)."; exit 3 }
Write-Host "  host exe: $hostExe"

# --- launch headless -------------------------------------------------------
Write-Host "`n--- launch leopard-host --headless on :$Port ---" -ForegroundColor Cyan
$hostProc = Start-Process -FilePath $hostExe -ArgumentList '--headless' -PassThru `
    -RedirectStandardOutput (Join-Path $runDir 'leopard-host.out.log') `
    -RedirectStandardError  (Join-Path $runDir 'leopard-host.err.log')

$ready = $false
foreach ($attempt in 1..30) {
    Start-Sleep -Milliseconds 500
    try {
        $h = Invoke-RestMethod -Uri "$base/api/health" -TimeoutSec 3 -ErrorAction Stop
        if ($h.ok) { $ready = $true; break }
    } catch { }
}
if (-not $ready) {
    if (-not $hostProc.HasExited) { Stop-Process -Id $hostProc.Id -Force }
    Write-Error "leopard-host did not become healthy on $base. See leopard-host.err.log."
    exit 3
}
Write-Host "  healthy." -ForegroundColor Green

# Point leopard-host's config at our LogsDir so /api/parse resolves names there.
try {
    Invoke-RestMethod -Uri "$base/api/config" -Method Put -ContentType 'application/json' `
        -Body (@{ logDir = $LogsDir } | ConvertTo-Json) -TimeoutSec 10 | Out-Null
} catch { Write-Host "  (config PUT warning: $($_.Exception.Message))" -ForegroundColor Yellow }

# --- per-log sweep ---------------------------------------------------------
$rows = @()
try {
    foreach ($log in $logs) {
        Write-Host "`n--- $($log.Name) ($([math]::Round($log.Length/1MB,0)) MB) ---" -ForegroundColor Cyan
        $mtimeUtc = $log.LastWriteTimeUtc

        $psw = [System.Diagnostics.Stopwatch]::StartNew()
        $parseResp = $null
        try {
            $parseResp = Invoke-RestMethod -Uri "$base/api/parse" -Method Post -ContentType 'application/json' `
                -Body (@{ names = @($log.Name) } | ConvertTo-Json) -TimeoutSec 1800 -ErrorAction Stop
        } catch {
            Write-Host "  parse POST failed: $($_.Exception.Message)" -ForegroundColor Red
        }
        $psw.Stop()
        $deriveSec = [math]::Round($psw.Elapsed.TotalSeconds, 1)
        $okFlag = $false
        if ($parseResp -and $parseResp.results) { $okFlag = [bool]($parseResp.results[0].ok) }

        # artifact presence + sizes
        $present = @{}; $missing = @(); $totalBytes = 0
        foreach ($k in $artifactKinds) {
            $cp = Cache-Path $log.Name $mtimeUtc $k
            if (Test-Path $cp) { $sz = (Get-Item $cp).Length; $present[$k] = $sz; $totalBytes += $sz }
            else { $missing += $k }
        }

        # schema spot-check on the math artifacts
        $schemaFlags = @()
        function HasKey([string]$kind, [string[]]$needles) {
            $cp = Cache-Path $log.Name $mtimeUtc $kind
            if (-not (Test-Path $cp)) { return $false }
            $txt = Get-Content -Path $cp -Raw
            foreach ($n in $needles) { if ($txt -notmatch [regex]::Escape($n)) { return $false } }
            return $true
        }
        if ($present.ContainsKey('signals.v1.json')  -and -not (HasKey 'signals.v1.json'  @('spacing','followership','entropy'))) { $schemaFlags += 'signals-keys' }
        if ($present.ContainsKey('players.v1.json')   -and -not (HasKey 'players.v1.json'   @('pullId'))) { $schemaFlags += 'players-keys' }
        if ($present.ContainsKey('classify.v1.json')  -and -not (HasKey 'classify.v1.json'  @('outcome'))) { $schemaFlags += 'classify-keys' }

        $flags = @()
        if (-not $okFlag)            { $flags += 'PARSE-NOT-OK' }
        if ($missing.Count -gt 0)    { $flags += "MISSING:$($missing.Count)" }
        if ($schemaFlags.Count -gt 0){ $flags += ($schemaFlags -join ',') }

        Write-Host ("  derive={0}s  artifacts={1}/{2}  size={3:N1} MB  {4}" -f `
            $deriveSec, $present.Count, $artifactKinds.Count, ($totalBytes/1MB), `
            ($(if ($flags) { ($flags -join ',') } else { 'OK' })))

        $rows += [ordered]@{
            log            = $log.Name
            sizeMB         = [math]::Round($log.Length/1MB, 1)
            parseOk        = $okFlag
            deriveSec      = $deriveSec
            artifactsFound = $present.Count
            artifactsTotal = $artifactKinds.Count
            artifactBytes  = $totalBytes
            missing        = $missing
            flags          = $flags
        }
    }

    # --- determinism: TWICE-FRESH derive, byte-diff -------------------------
    # Both derives are wipe-then-derive with THIS build, so the comparison can't be fooled
    # by stale on-disk artifacts from an older build (the trap that bit the first run).
    Write-Host "`n--- determinism (twice-fresh): $($detTarget.Name) ---" -ForegroundColor Cyan
    $detMtime = $detTarget.LastWriteTimeUtc
    function Wipe-DetCache {
        foreach ($k in $artifactKinds) {
            $cp = Cache-Path $detTarget.Name $detMtime $k
            if (Test-Path $cp) { Remove-Item $cp -Force }
        }
    }
    function Derive-DetHashes {
        Wipe-DetCache
        Invoke-RestMethod -Uri "$base/api/parse" -Method Post -ContentType 'application/json' `
            -Body (@{ names = @($detTarget.Name) } | ConvertTo-Json) -TimeoutSec 1800 | Out-Null
        $h = @{}
        foreach ($k in $artifactKinds) {
            $cp = Cache-Path $detTarget.Name $detMtime $k
            if (Test-Path $cp) { $h[$k] = (Get-FileHash -Algorithm SHA256 $cp).Hash }
        }
        return $h
    }
    $first  = Derive-DetHashes
    $second = Derive-DetHashes
    $detDiffs = @()
    foreach ($k in $artifactKinds) {
        $a = $first[$k]; $b = $second[$k]
        if ($a -or $b) { if ($a -ne $b) { $detDiffs += $k } }
    }
    $determinismOk = ($detDiffs.Count -eq 0) -and ($first.Count -eq $artifactKinds.Count)
    $detNote = if ($first.Count -ne $artifactKinds.Count) { " (only $($first.Count)/$($artifactKinds.Count) artifacts derived)" } else { '' }
    Write-Host "  determinism: $(if ($detDiffs.Count -eq 0) {"IDENTICAL$detNote"} else {"DIVERGED on $($detDiffs -join ',')"})" `
        -ForegroundColor $(if ($determinismOk) {'Green'} else {'Red'})

    # --- cache-skip: re-POST with full cache present, confirm no rewrite -----
    Write-Host "`n--- cache-skip check: $($detTarget.Name) ---" -ForegroundColor Cyan
    $sigPath = Cache-Path $detTarget.Name $detMtime 'signals.v1.json'
    $writeBefore = if (Test-Path $sigPath) { (Get-Item $sigPath).LastWriteTimeUtc } else { $null }
    Invoke-RestMethod -Uri "$base/api/parse" -Method Post -ContentType 'application/json' `
        -Body (@{ names = @($detTarget.Name) } | ConvertTo-Json) -TimeoutSec 120 | Out-Null
    $writeAfter = if (Test-Path $sigPath) { (Get-Item $sigPath).LastWriteTimeUtc } else { $null }
    $cacheSkipOk = ($null -ne $writeBefore) -and ($writeBefore -eq $writeAfter)
    Write-Host "  cache-skip: $(if ($cacheSkipOk) {'NO REWRITE (hit)'} else {'REWROTE (miss!)'})" `
        -ForegroundColor $(if ($cacheSkipOk) {'Green'} else {'Red'})
}
finally {
    if ($hostProc -and -not $hostProc.HasExited) {
        Write-Host "`n--- stopping leopard-host ---" -ForegroundColor Cyan
        Stop-Process -Id $hostProc.Id -Force
    }
}

$sw.Stop()

# --- results ---------------------------------------------------------------
$result = [ordered]@{
    runId         = "leopard-artifact-$stamp"
    generatedAt   = (Get-Date).ToString('o')
    leopardRoot   = $LeopardRoot
    cacheDir      = $cacheDir
    durationSec   = [math]::Round($sw.Elapsed.TotalSeconds, 1)
    determinismOk = $determinismOk
    cacheSkipOk   = $cacheSkipOk
    detTarget     = $detTarget.Name
    logs          = $rows
}
Write-JsonNoBom $result (Join-Path $runDir 'leopard-results.json')

# --- summary.md ------------------------------------------------------------
$flagged = @($rows | Where-Object { $_.flags.Count -gt 0 })
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# Leopard artifact + cache sweep -- $stamp")
[void]$sb.AppendLine()
[void]$sb.AppendLine("- Duration: $($result.durationSec)s | Logs: $($rows.Count) | Flagged: $($flagged.Count)")
[void]$sb.AppendLine("- Determinism ($($detTarget.Name)): $(if ($determinismOk) {'**IDENTICAL**'} else {'**DIVERGED**'})")
[void]$sb.AppendLine("- Cache-skip: $(if ($cacheSkipOk) {'**hit (no rewrite)**'} else {'**miss (rewrote)**'})")
[void]$sb.AppendLine()
[void]$sb.AppendLine("| Log | MB | ParseOK | Derive s | Artifacts | Size MB | Flags |")
[void]$sb.AppendLine("|---|---:|:--:|---:|---:|---:|---|")
foreach ($r in $rows) {
    $fl = if ($r.flags.Count -gt 0) { ($r.flags -join ',') } else { 'OK' }
    [void]$sb.AppendLine("| $($r.log) | $($r.sizeMB) | $($r.parseOk) | $($r.deriveSec) | $($r.artifactsFound)/$($r.artifactsTotal) | $([math]::Round($r.artifactBytes/1MB,1)) | $fl |")
}
Write-TextNoBom ($sb.ToString()) (Join-Path $runDir 'leopard-summary.md')

Write-Host "`n=== DONE ===" -ForegroundColor Green
Write-Host "leopard-results.json + leopard-summary.md -> $runDir"
if ($flagged.Count -gt 0) { Write-Host "FLAGGED: $($flagged.Count) log(s) need review." -ForegroundColor Yellow }
