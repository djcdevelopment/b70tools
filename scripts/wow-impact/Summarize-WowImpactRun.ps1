param(
    [Parameter(Mandatory=$true)] [string]$RunDir,
    [string]$B70Tools = 'D:\work\b70tools\build\b70tools.exe',
    [string]$FrameCsvPath = ''
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NoBom.ps1')

if (-not (Test-Path $RunDir)) { throw "run directory not found: $RunDir" }

$manifestPath = Join-Path $RunDir 'manifest.json'
$manifest = $null
if (Test-Path $manifestPath) {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Get-JsonLines {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return @() }
    $items = New-Object System.Collections.ArrayList
    Get-Content -LiteralPath $Path -Encoding UTF8 | ForEach-Object {
        if (-not $_) { return }
        try { [void]$items.Add(($_ | ConvertFrom-Json -ErrorAction Stop)) } catch {}
    }
    return @($items)
}

function Get-Percentile {
    param([double[]]$Values, [double]$Percentile)
    if (-not $Values -or $Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 1) { return [math]::Round($sorted[0], 3) }
    $rank = ($Percentile / 100.0) * ($sorted.Count - 1)
    $low = [math]::Floor($rank)
    $high = [math]::Ceiling($rank)
    if ($low -eq $high) { return [math]::Round($sorted[$low], 3) }
    $weight = $rank - $low
    return [math]::Round(($sorted[$low] * (1 - $weight)) + ($sorted[$high] * $weight), 3)
}

function Get-FrameStats {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path $Path)) { return $null }
    $rows = @(Import-Csv -LiteralPath $Path)
    if ($rows.Count -eq 0) { return $null }

    $columns = @($rows[0].PSObject.Properties.Name)
    $preferred = @('MsBetweenPresents', 'MsUntilDisplayed', 'FrameTimeMs', 'frameTimeMs', 'FrameTime', 'msBetweenPresents')
    $column = $preferred | Where-Object { $columns -contains $_ } | Select-Object -First 1
    if (-not $column) {
        foreach ($c in $columns) {
            if ($c -match 'ms|frame.*time|present') { $column = $c; break }
        }
    }
    if (-not $column) { return [pscustomobject]@{ error = 'no frame-time-like column found'; path = $Path } }

    $values = @()
    foreach ($r in $rows) {
        $v = $r.$column
        if ($null -eq $v -or $v -eq '') { continue }
        $d = 0.0
        if ([double]::TryParse([string]$v, [ref]$d) -and $d -gt 0 -and $d -lt 10000) {
            $values += $d
        }
    }
    if ($values.Count -eq 0) { return [pscustomobject]@{ error = 'no numeric frame times found'; path = $Path; column = $column } }

    return [pscustomobject]@{
        path = $Path
        column = $column
        frames = $values.Count
        avg_ms = [math]::Round((($values | Measure-Object -Average).Average), 3)
        p50_ms = Get-Percentile -Values $values -Percentile 50
        p95_ms = Get-Percentile -Values $values -Percentile 95
        p99_ms = Get-Percentile -Values $values -Percentile 99
        max_ms = [math]::Round((($values | Measure-Object -Maximum).Maximum), 3)
        over_33ms = @($values | Where-Object { $_ -gt 33.3 }).Count
        over_50ms = @($values | Where-Object { $_ -gt 50.0 }).Count
        over_100ms = @($values | Where-Object { $_ -gt 100.0 }).Count
    }
}

function Get-DpcStats {
    param([string]$Path)
    if (-not $Path -or -not (Test-Path $Path)) { return $null }
    $rows = @(Get-JsonLines -Path $Path | Where-Object { $_.kind -eq 'dpc_sample' })
    if ($rows.Count -eq 0) { return $null }
    $dpc = @($rows | Where-Object { $null -ne $_.dpc_pct } | ForEach-Object { [double]$_.dpc_pct })
    $interrupt = @($rows | Where-Object { $null -ne $_.interrupt_pct } | ForEach-Object { [double]$_.interrupt_pct })
    $queue = @($rows | Where-Object { $null -ne $_.queue_length } | ForEach-Object { [double]$_.queue_length })
    return [pscustomobject]@{
        path = $Path
        samples = $rows.Count
        avg_dpc_pct = if ($dpc.Count -gt 0) { [math]::Round((($dpc | Measure-Object -Average).Average), 3) } else { $null }
        max_dpc_pct = if ($dpc.Count -gt 0) { [math]::Round((($dpc | Measure-Object -Maximum).Maximum), 3) } else { $null }
        avg_interrupt_pct = if ($interrupt.Count -gt 0) { [math]::Round((($interrupt | Measure-Object -Average).Average), 3) } else { $null }
        max_interrupt_pct = if ($interrupt.Count -gt 0) { [math]::Round((($interrupt | Measure-Object -Maximum).Maximum), 3) } else { $null }
        max_queue_length = if ($queue.Count -gt 0) { [math]::Round((($queue | Measure-Object -Maximum).Maximum), 3) } else { $null }
    }
}

if (Test-Path $B70Tools) {
    if (-not (Test-Path (Join-Path $RunDir 'b70tools-adapters.txt'))) {
        Write-TextNoBom -Path (Join-Path $RunDir 'b70tools-adapters.txt') -Content ((& $B70Tools adapters $RunDir 2>&1 | Out-String))
    }
    if (-not (Test-Path (Join-Path $RunDir 'b70tools-summary.txt'))) {
        Write-TextNoBom -Path (Join-Path $RunDir 'b70tools-summary.txt') -Content ((& $B70Tools summarize $RunDir 2>&1 | Out-String))
    }
    if (-not (Test-Path (Join-Path $RunDir 'b70tools-disagreements.txt'))) {
        Write-TextNoBom -Path (Join-Path $RunDir 'b70tools-disagreements.txt') -Content ((& $B70Tools disagreements $RunDir 2>&1 | Out-String))
    }
    if (-not (Test-Path (Join-Path $RunDir 'b70tools-self.txt'))) {
        Write-TextNoBom -Path (Join-Path $RunDir 'b70tools-self.txt') -Content ((& $B70Tools self $RunDir 2>&1 | Out-String))
    }
}

$hostPath = Join-Path $RunDir 'host-pressure.jsonl'
$hostSamples = @(Get-JsonLines -Path $hostPath)
$hostStats = $null
if ($hostSamples.Count -gt 0) {
    $free = @($hostSamples | ForEach-Object { [double]$_.free_ram_gb })
    $used = @($hostSamples | ForEach-Object { [double]$_.used_ram_gb })
    $pf = @($hostSamples | Where-Object { $null -ne $_.pagefile_current_mb } | ForEach-Object { [double]$_.pagefile_current_mb })
    $commit = @($hostSamples | Where-Object { $null -ne $_.committed_gb } | ForEach-Object { [double]$_.committed_gb })
    $hostStats = [pscustomobject]@{
        samples = $hostSamples.Count
        min_free_ram_gb = [math]::Round((($free | Measure-Object -Minimum).Minimum), 3)
        max_used_ram_gb = [math]::Round((($used | Measure-Object -Maximum).Maximum), 3)
        pagefile_start_mb = if ($pf.Count -gt 0) { [math]::Round($pf[0], 2) } else { $null }
        pagefile_end_mb = if ($pf.Count -gt 0) { [math]::Round($pf[-1], 2) } else { $null }
        pagefile_delta_mb = if ($pf.Count -gt 0) { [math]::Round($pf[-1] - $pf[0], 2) } else { $null }
        committed_start_gb = if ($commit.Count -gt 0) { [math]::Round($commit[0], 3) } else { $null }
        committed_end_gb = if ($commit.Count -gt 0) { [math]::Round($commit[-1], 3) } else { $null }
        committed_delta_gb = if ($commit.Count -gt 0) { [math]::Round($commit[-1] - $commit[0], 3) } else { $null }
    }
}

$requestsPath = Join-Path $RunDir 'inference-requests.jsonl'
$requests = @(Get-JsonLines -Path $requestsPath)
$requestStats = $null
if ($requests.Count -gt 0) {
    $latencies = @($requests | Where-Object { $_.wall_ms } | ForEach-Object { [double]$_.wall_ms })
    $requestStats = [pscustomobject]@{
        count = $requests.Count
        ok = @($requests | Where-Object { $_.status -eq 'ok' }).Count
        error = @($requests | Where-Object { $_.status -eq 'error' }).Count
        p50_wall_ms = Get-Percentile -Values $latencies -Percentile 50
        p95_wall_ms = Get-Percentile -Values $latencies -Percentile 95
        max_wall_ms = if ($latencies.Count -gt 0) { [math]::Round((($latencies | Measure-Object -Maximum).Maximum), 0) } else { $null }
    }
}

$wowLogTailPath = Join-Path $RunDir 'wow-log-tail.jsonl'
$wowLogTail = @(Get-JsonLines -Path $wowLogTailPath)
$wowLogStats = $null
if ($wowLogTail.Count -gt 0) {
    $samples = @($wowLogTail | Where-Object { $_.kind -eq 'tail_sample' })
    $bytes = @($samples | ForEach-Object { [long]$_.delta_bytes })
    $lines = @($samples | ForEach-Object { [long]$_.delta_lines })
    $lastNonEmpty = $samples | Where-Object { $_.delta_lines -gt 0 } | Select-Object -Last 1
    $wowLogStats = [pscustomobject]@{
        samples = $samples.Count
        total_delta_bytes = if ($bytes.Count -gt 0) { ($bytes | Measure-Object -Sum).Sum } else { 0 }
        total_delta_lines = if ($lines.Count -gt 0) { ($lines | Measure-Object -Sum).Sum } else { 0 }
        max_delta_lines = if ($lines.Count -gt 0) { ($lines | Measure-Object -Maximum).Maximum } else { 0 }
        last_line = if ($lastNonEmpty) { $lastNonEmpty.last_line } else { $null }
    }
}

$dpcPath = Join-Path $RunDir 'frame-impact.dpc.jsonl'
$dpcStats = Get-DpcStats -Path $dpcPath

if (-not $FrameCsvPath -and $manifest -and $manifest.frame_csv_path) {
    $FrameCsvPath = [string]$manifest.frame_csv_path
}
if (-not $FrameCsvPath) {
    $candidate = Join-Path $RunDir 'frame-times.csv'
    if (Test-Path $candidate) { $FrameCsvPath = $candidate }
}
$frameStats = Get-FrameStats -Path $FrameCsvPath

$summary = [ordered]@{
    run_dir = $RunDir
    scenario = if ($manifest) { $manifest.scenario } else { $null }
    started_at = if ($manifest) { $manifest.started_at } else { $null }
    host_pressure = $hostStats
    wow_log_tail = $wowLogStats
    dpc = $dpcStats
    inference = $requestStats
    frame_times = $frameStats
}
Write-JsonNoBom -Path (Join-Path $RunDir 'summary.json') -Value $summary -Depth 12

$md = New-Object System.Text.StringBuilder
[void]$md.AppendLine("# WoW impact run summary")
[void]$md.AppendLine("")
[void]$md.AppendLine("- Run: ``$RunDir``")
if ($manifest) {
    [void]$md.AppendLine("- Scenario: $($manifest.scenario)")
    [void]$md.AppendLine("- Started: $($manifest.started_at)")
}
[void]$md.AppendLine("")

[void]$md.AppendLine("## Frame Times")
if ($frameStats -and -not $frameStats.error) {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("| metric | value |")
    [void]$md.AppendLine("|---|---:|")
    [void]$md.AppendLine("| frames | $($frameStats.frames) |")
    [void]$md.AppendLine("| avg ms | $($frameStats.avg_ms) |")
    [void]$md.AppendLine("| p50 ms | $($frameStats.p50_ms) |")
    [void]$md.AppendLine("| p95 ms | $($frameStats.p95_ms) |")
    [void]$md.AppendLine("| p99 ms | $($frameStats.p99_ms) |")
    [void]$md.AppendLine("| max ms | $($frameStats.max_ms) |")
    [void]$md.AppendLine("| >33.3 ms | $($frameStats.over_33ms) |")
    [void]$md.AppendLine("| >50 ms | $($frameStats.over_50ms) |")
    [void]$md.AppendLine("| >100 ms | $($frameStats.over_100ms) |")
} elseif ($frameStats -and $frameStats.error) {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("Frame CSV found, but could not summarize it: $($frameStats.error)")
} else {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("No frame-time CSV was found. This run cannot answer game impact by itself.")
}
[void]$md.AppendLine("")

[void]$md.AppendLine("## Host Pressure")
if ($hostStats) {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("- Samples: $($hostStats.samples)")
    [void]$md.AppendLine("- Minimum free RAM: $($hostStats.min_free_ram_gb) GB")
    [void]$md.AppendLine("- Maximum used RAM: $($hostStats.max_used_ram_gb) GB")
    if ($null -ne $hostStats.pagefile_delta_mb) {
        [void]$md.AppendLine("- Pagefile delta: $($hostStats.pagefile_delta_mb) MB")
    } else {
        [void]$md.AppendLine("- Pagefile delta: unavailable")
    }
    if ($null -ne $hostStats.committed_delta_gb) {
        [void]$md.AppendLine("- Commit delta: $($hostStats.committed_delta_gb) GB")
    }
} else {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("No host-pressure samples found.")
}
[void]$md.AppendLine("")

[void]$md.AppendLine("## Combat Log Tail")
if ($wowLogStats) {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("- Samples: $($wowLogStats.samples)")
    [void]$md.AppendLine("- New lines: $($wowLogStats.total_delta_lines)")
    [void]$md.AppendLine("- New bytes: $($wowLogStats.total_delta_bytes)")
    [void]$md.AppendLine("- Max lines in one sample: $($wowLogStats.max_delta_lines)")
    if ($wowLogStats.last_line) {
        [void]$md.AppendLine("- Last observed line: ``$($wowLogStats.last_line)``")
    }
} else {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("No combat-log tail samples found.")
}
[void]$md.AppendLine("")

[void]$md.AppendLine("## DPC")
if ($dpcStats) {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("| metric | value |")
    [void]$md.AppendLine("|---|---:|")
    [void]$md.AppendLine("| samples | $($dpcStats.samples) |")
    [void]$md.AppendLine("| avg DPC % | $($dpcStats.avg_dpc_pct) |")
    [void]$md.AppendLine("| max DPC % | $($dpcStats.max_dpc_pct) |")
    [void]$md.AppendLine("| avg interrupt % | $($dpcStats.avg_interrupt_pct) |")
    [void]$md.AppendLine("| max interrupt % | $($dpcStats.max_interrupt_pct) |")
    [void]$md.AppendLine("| max queue length | $($dpcStats.max_queue_length) |")
} else {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("No DPC sample JSONL was found.")
}
[void]$md.AppendLine("")

[void]$md.AppendLine("## Inference")
if ($requestStats) {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("- Requests: $($requestStats.count)")
    [void]$md.AppendLine("- OK: $($requestStats.ok)")
    [void]$md.AppendLine("- Errors: $($requestStats.error)")
    [void]$md.AppendLine("- p50 wall: $($requestStats.p50_wall_ms) ms")
    [void]$md.AppendLine("- p95 wall: $($requestStats.p95_wall_ms) ms")
    [void]$md.AppendLine("- max wall: $($requestStats.max_wall_ms) ms")
} else {
    [void]$md.AppendLine("")
    [void]$md.AppendLine("No inference request records found.")
}
[void]$md.AppendLine("")

[void]$md.AppendLine("## b70tools")
[void]$md.AppendLine("")
[void]$md.AppendLine("Generated artifacts:")
foreach ($name in @('b70tools-adapters.txt', 'b70tools-summary.txt', 'b70tools-disagreements.txt', 'b70tools-self.txt')) {
    if (Test-Path (Join-Path $RunDir $name)) {
        [void]$md.AppendLine("- ``$name``")
    }
}

$summaryMd = Join-Path $RunDir 'summary.md'
Write-TextNoBom -Path $summaryMd -Content ($md.ToString())

Write-Output "[summary] $summaryMd"
Write-Output "[summary-json] $(Join-Path $RunDir 'summary.json')"
