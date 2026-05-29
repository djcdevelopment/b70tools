# preflight.ps1 — load-distribution + safety validation gate for the eval runner.
#
# DESIGN CONSTRAINT (hardware-confirmed):
#   adapter_00012fbe has a broken IGCL handle and emits NO ongoing telemetry
#   (no temperature, energy, activity, or frequency). DXGI VRAM metrics are
#   per-process budgets (b70tools sees only its own commit, not llama-server's),
#   so they cannot detect cross-process VRAM hogging.
#
#   We work around this with TWO INDEPENDENT signals:
#
#   (1) HOST RAM   - Win32_OperatingSystem.FreePhysicalMemory. System-wide,
#                    process-agnostic, no driver dependency. Shared GPU Memory
#                    fallback ALWAYS manifests as host RAM commit climbing into
#                    the 20+ GB range. This is the MANDATORY safety gate -
#                    refusing to proceed when host RAM is unsafe protects the
#                    rig from the OOM / non-POST / BIOS-reflash cascade.
#
#   (2) IGCL on adapter_00011b4f - the healthy card's energy and engine-activity
#                    counters. If THIS card shows no work during a workload
#                    window, the model is either fully on the other card (broken
#                    "single-card hog" pattern) OR the workload never started.
#                    ADVISORY because we have no direct visibility into 00012fbe.
#
# Usage (standalone audit of an existing run):
#   .\preflight.ps1 -EventsPath D:\work\b70tools\eval\runs\<config>\telemetry\events.jsonl
#
# Usage (host-only safety check, no telemetry needed):
#   .\preflight.ps1 -CheckHostMemoryOnly
#
# Usage (dot-sourced from run-eval.ps1):
#   . .\preflight.ps1
#   $mem = Test-HostMemorySafety
#   if (-not $mem.ok) { throw "preflight failed: $($mem.issues -join '; ')" }
#   $work = Test-AdapterWorkSignature -EventsPath $TelemetryDir\events.jsonl `
#                                     -WindowStartUnixMs $loadCompletedMs
#   ...
#
# THRESHOLDS (defaults, tunable):
#   MaxHostUsedGb = 26.0   - 32 GB rig, 6 GB headroom for OS + processes
#   MinAdapter0AvgW = 30.0 - idle is ~25-28 W; workload pulls to 80-200 W
#   MinAdapter0ActivityPct = 5.0  - idle is ~1-2% engine-active fraction

param(
    [string]$EventsPath              = '',
    [double]$MaxHostUsedGb           = 26.0,
    [double]$MinAdapter0AvgW         = 30.0,
    [double]$MinAdapter0ActivityPct  = 5.0,
    [string]$HealthyAdapter          = 'adapter_00011b4f',
    [string]$BrokenIgclAdapter       = 'adapter_00012fbe',
    [switch]$CheckHostMemoryOnly,
    [switch]$Json
)

function Test-HostMemorySafety {
    param([double]$MaxUsedGb = 26.0)
    $os = Get-CimInstance Win32_OperatingSystem
    $totalGb = [math]::Round($os.TotalVisibleMemorySize / 1MB, 2)
    $freeGb  = [math]::Round($os.FreePhysicalMemory     / 1MB, 2)
    $usedGb  = [math]::Round($totalGb - $freeGb, 2)
    $headroom = [math]::Round($MaxUsedGb - $usedGb, 2)
    $ok = $usedGb -le $MaxUsedGb

    $issues = New-Object System.Collections.ArrayList
    if (-not $ok) {
        [void]$issues.Add(
            "host RAM used $usedGb GB exceeds safety threshold $MaxUsedGb GB (free $freeGb GB) - Shared GPU Memory fallback suspected; proceeding risks OOM cascade and non-POST / BIOS-reflash on next cold boot"
        )
    }
    return [pscustomobject]@{
        status        = if ($ok) { 'safe' } else { 'unsafe' }
        ok            = $ok
        used_gb       = $usedGb
        free_gb       = $freeGb
        total_gb      = $totalGb
        threshold_gb  = $MaxUsedGb
        headroom_gb   = $headroom
        issues        = @($issues)
        evaluated_at  = (Get-Date).ToString('o')
    }
}

function Read-B70Events {
    param([Parameter(Mandatory)][string]$Path)
    Get-Content -LiteralPath $Path -Encoding UTF8 |
        Where-Object { $_ -and $_.StartsWith('{') } |
        ForEach-Object {
            try { $_ | ConvertFrom-Json -ErrorAction Stop } catch { $null }
        } |
        Where-Object { $_ -ne $null }
}

function Get-AdapterMetricSeries {
    param($Events, [Parameter(Mandatory)][string]$AdapterId, [Parameter(Mandatory)][string]$MetricName)
    $out = New-Object System.Collections.ArrayList
    foreach ($e in $Events) {
        if ($e.k -ne 'ms') { continue }
        if ($e.a -ne $AdapterId) { continue }
        if ($e.n -ne $MetricName) { continue }
        [void]$out.Add([pscustomobject]@{ t = [long]$e.t; v = [double]$e.v })
    }
    return ,$out
}

function Test-AdapterWorkSignature {
    # Examines the healthy-IGCL adapter's energy + activity counters for evidence
    # that it engaged in the workload. If it stayed near idle the whole time,
    # either the model is fully on the OTHER card (single-card hog) or the
    # workload never ran.
    #
    # Counters are cumulative; we read the FIRST + LAST samples in the file and
    # compute mean power (J/s = W) and mean engine-active fraction.
    param(
        [Parameter(Mandatory)][string]$EventsPath,
        [string]$HealthyAdapter         = 'adapter_00011b4f',
        [double]$MinAvgW                = 30.0,
        [double]$MinActivityPct         = 5.0
    )
    if (-not (Test-Path $EventsPath)) {
        return [pscustomobject]@{
            status = 'no-telemetry'; ok = $false
            issues = @("events.jsonl not found at: $EventsPath")
        }
    }
    $events = @(Read-B70Events -Path $EventsPath)
    if ($events.Count -eq 0) {
        return [pscustomobject]@{
            status = 'no-events'; ok = $false
            issues = @('events.jsonl was empty or contained no parseable records')
        }
    }

    $energy   = Get-AdapterMetricSeries -Events $events -AdapterId $HealthyAdapter -MetricName 'gpu.energy_j_counter'
    $activity = Get-AdapterMetricSeries -Events $events -AdapterId $HealthyAdapter -MetricName 'gpu.activity.render_compute_counter'

    if ($energy.Count -lt 2 -or $activity.Count -lt 2) {
        return [pscustomobject]@{
            status = 'insufficient-samples'; ok = $false
            issues = @("not enough IGCL samples for $HealthyAdapter (energy=$($energy.Count), activity=$($activity.Count)); is b70tools running with the IGCL collector enabled?")
            energy_samples   = $energy.Count
            activity_samples = $activity.Count
        }
    }

    $eFirst = $energy[0]; $eLast = $energy[-1]
    $aFirst = $activity[0]; $aLast = $activity[-1]
    $windowSec    = [double]($eLast.t - $eFirst.t) / 1e9
    if ($windowSec -le 0) {
        return [pscustomobject]@{
            status = 'zero-duration'; ok = $false
            issues = @("energy samples have non-positive time span ($windowSec s)")
        }
    }
    $deltaJ       = $eLast.v - $eFirst.v
    $avgW         = if ($windowSec -gt 0) { [math]::Round($deltaJ / $windowSec, 2) } else { 0 }
    $deltaActNs   = $aLast.v - $aFirst.v
    $activityPct  = [math]::Round(($deltaActNs / 1e9) / $windowSec * 100.0, 2)

    $issues = New-Object System.Collections.ArrayList
    if ($avgW -lt $MinAvgW) {
        [void]$issues.Add(
            "$HealthyAdapter average power $avgW W is below working threshold $MinAvgW W during $([math]::Round($windowSec,1)) s window - card 0 appears idle => model likely landed on adapter_00012fbe only (single-card hog) OR workload never started"
        )
    }
    if ($activityPct -lt $MinActivityPct) {
        [void]$issues.Add(
            "$HealthyAdapter engine-activity $activityPct% is below working threshold $MinActivityPct% during $([math]::Round($windowSec,1)) s window - same diagnosis as above"
        )
    }

    $ok = ($issues.Count -eq 0)
    return [pscustomobject]@{
        status            = if ($ok) { 'card-0-working' } else { 'card-0-idle-or-undriven' }
        ok                = $ok
        adapter           = $HealthyAdapter
        window_s          = [math]::Round($windowSec, 1)
        avg_w             = $avgW
        activity_pct      = $activityPct
        energy_delta_j    = [math]::Round($deltaJ, 1)
        activity_delta_ms = [math]::Round($deltaActNs / 1e6, 1)
        thresholds        = [pscustomobject]@{
            min_avg_w        = $MinAvgW
            min_activity_pct = $MinActivityPct
        }
        issues            = @($issues)
        # Always flag visibility limitation on the broken-IGCL adapter:
        note              = "adapter_00012fbe has broken IGCL and is unobservable via b70tools - cross-check host RAM (Test-HostMemorySafety) for Shared-GPU-Memory detection"
    }
}

function Test-LoadDistribution {
    # Combined verdict: host memory safety (MANDATORY) + adapter work signature
    # (ADVISORY). Returns ok = mandatory_ok AND advisory_ok.
    param(
        [Parameter(Mandatory)][string]$EventsPath,
        [double]$MaxHostUsedGb           = 26.0,
        [double]$MinAdapter0AvgW         = 30.0,
        [double]$MinAdapter0ActivityPct  = 5.0,
        [string]$HealthyAdapter          = 'adapter_00011b4f'
    )
    $mem  = Test-HostMemorySafety -MaxUsedGb $MaxHostUsedGb
    $work = Test-AdapterWorkSignature -EventsPath $EventsPath `
                                      -HealthyAdapter $HealthyAdapter `
                                      -MinAvgW $MinAdapter0AvgW `
                                      -MinActivityPct $MinAdapter0ActivityPct
    $combined = $mem.ok -and $work.ok
    $allIssues = New-Object System.Collections.ArrayList
    foreach ($i in $mem.issues)  { [void]$allIssues.Add('[host-ram] '   + $i) }
    foreach ($i in $work.issues) { [void]$allIssues.Add('[card-work] ' + $i) }

    return [pscustomobject]@{
        status        = if ($combined) { 'healthy' } else { 'broken' }
        ok            = $combined
        mandatory_ok  = $mem.ok
        advisory_ok   = $work.ok
        issues        = @($allIssues)
        host_memory   = $mem
        adapter_work  = $work
        evaluated_at  = (Get-Date).ToString('o')
    }
}

# ---------------------------------------------------------------------------
# Standalone runner.
# ---------------------------------------------------------------------------
if ($CheckHostMemoryOnly) {
    $result = Test-HostMemorySafety -MaxUsedGb $MaxHostUsedGb
    if ($Json) {
        $result | ConvertTo-Json -Depth 10
    } else {
        Write-Output ('Host memory: ' + $result.status + ' (used=' + $result.used_gb + ' GB / total=' + $result.total_gb + ' GB, free=' + $result.free_gb + ' GB, threshold=' + $result.threshold_gb + ' GB, headroom=' + $result.headroom_gb + ' GB)')
        foreach ($i in $result.issues) { Write-Output ('  - ' + $i) }
    }
    if (-not $result.ok) { exit 2 } else { exit 0 }
}

if ($EventsPath) {
    $result = Test-LoadDistribution -EventsPath $EventsPath `
                                    -MaxHostUsedGb $MaxHostUsedGb `
                                    -MinAdapter0AvgW $MinAdapter0AvgW `
                                    -MinAdapter0ActivityPct $MinAdapter0ActivityPct `
                                    -HealthyAdapter $HealthyAdapter
    if ($Json) {
        $result | ConvertTo-Json -Depth 10
    } else {
        Write-Output ('Verdict: ' + $result.status + ' (ok=' + $result.ok + ', mandatory_ok=' + $result.mandatory_ok + ', advisory_ok=' + $result.advisory_ok + ')')
        Write-Output ''
        Write-Output ('Host RAM: used=' + $result.host_memory.used_gb + ' GB / threshold=' + $result.host_memory.threshold_gb + ' GB (headroom=' + $result.host_memory.headroom_gb + ' GB)')
        Write-Output ('Adapter ' + $result.adapter_work.adapter + ' (healthy IGCL): avg=' + $result.adapter_work.avg_w + ' W, activity=' + $result.adapter_work.activity_pct + '%, window=' + $result.adapter_work.window_s + ' s')
        Write-Output ('Note: ' + $result.adapter_work.note)
        if ($result.issues.Count -gt 0) {
            Write-Output ''
            Write-Output 'Issues:'
            foreach ($i in $result.issues) { Write-Output ('  - ' + $i) }
        }
    }
    if (-not $result.ok) { exit 2 } else { exit 0 }
}
