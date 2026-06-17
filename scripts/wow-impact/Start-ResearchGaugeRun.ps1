param(
    [Parameter(Mandatory=$true)] [string]$RequestedIdentity,
    [string]$GamingIdentity = '',
    [string]$Scenario = 'research-gauge-live',
    [int]$DurationSec = 300,
    [string]$RunRoot = 'D:\work\b70tools\runs',
    [string]$B70Tools = 'D:\work\b70tools\build\b70tools.exe',
    [string]$LlamaServerBin = 'D:\work\battlemage\llamacpp-win-vulkan\llama-server.exe',
    [string]$ModelPath = 'D:\work\battlemage\models\qwen2.5-14b-instruct-q4_K_M.gguf',
    [string]$HostName = '127.0.0.1',
    [int]$Port = 8080,
    [string]$Alias = 'tempo-b70-second',
    [string]$TempoRepo = 'C:\path\to\World of Warcraft\Tempo',
    [string]$WowLogPath = 'C:\path\to\World of Warcraft\_retail_\Logs\WoWCombatLog.txt',
    [switch]$TailWowLog,
    [double]$WowLogTailIntervalSec = 7.0,
    [string]$PresentMonPath = '',
    [string[]]$PresentMonArgs = @(),
    [string]$FrameCsvPath = '',
    [string[]]$HostProcessNames = @('Wow', 'WowT', 'WowClassic', 'llama-server', 'llama-cli', 'ollama', 'dotnet', 'Tempo.Host'),
    [double]$DpcIntervalSec = 1.0,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
. (Join-Path $scriptRoot 'NoBom.ps1')

$watchHostScript = Join-Path $scriptRoot 'Watch-HostPressure.ps1'
$watchLogScript = Join-Path $scriptRoot 'Watch-WowLogTail.ps1'
$watchDpcScript = Join-Path $scriptRoot 'Watch-DpcLatency.ps1'
$serverScript = Join-Path $scriptRoot 'Start-SecondB70LlamaServer.ps1'
$stimulusScript = Join-Path $scriptRoot 'Invoke-InferenceStimulus.ps1'
$summarizeScript = Join-Path $scriptRoot 'Summarize-WowImpactRun.ps1'

if (-not (Test-Path -LiteralPath $B70Tools)) { throw "b70tools not found: $B70Tools" }
if (-not (Test-Path -LiteralPath $LlamaServerBin)) { throw "llama-server not found: $LlamaServerBin" }
if (-not (Test-Path -LiteralPath $ModelPath)) { throw "model not found: $ModelPath" }
if (-not (Test-Path -LiteralPath $watchHostScript)) { throw "host pressure watcher not found: $watchHostScript" }
if (-not (Test-Path -LiteralPath $watchDpcScript)) { throw "DPC watcher not found: $watchDpcScript" }
if ($TailWowLog -and -not (Test-Path -LiteralPath $watchLogScript)) { throw "WoW log tailer not found: $watchLogScript" }
if (-not (Test-Path -LiteralPath $serverScript)) { throw "server launcher not found: $serverScript" }
if (-not (Test-Path -LiteralPath $stimulusScript)) { throw "stimulus launcher not found: $stimulusScript" }
if (-not (Test-Path -LiteralPath $summarizeScript)) { throw "summarizer not found: $summarizeScript" }

function Quote-Arg {
    param([Parameter(Mandatory=$true)] [string]$Value)
    return '"' + ($Value -replace '"', '\"') + '"'
}

function New-CommandLine {
    param([string[]]$Parts)
    return ($Parts | ForEach-Object {
        if ($_ -match '^\-[^-].*' -or $_ -match '^[A-Za-z0-9_.:-]+$') { $_ } else { Quote-Arg $_ }
    }) -join ' '
}

function Get-JsonLines {
    param([Parameter(Mandatory=$true)] [string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return @() }
    $items = New-Object System.Collections.ArrayList
    Get-Content -LiteralPath $Path -Encoding UTF8 | ForEach-Object {
        if (-not $_) { return }
        try { [void]$items.Add(($_ | ConvertFrom-Json -ErrorAction Stop)) } catch {}
    }
    return @($items)
}

function Get-TempEnumerationDir {
    param([Parameter(Mandatory=$true)] [string]$BaseDir)

    $dir = Join-Path $BaseDir ("_enum-" + [Guid]::NewGuid().ToString('n'))
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return $dir
}

function Get-EnumeratedAdapters {
    param(
        [Parameter(Mandatory=$true)] [string]$BinaryPath,
        [Parameter(Mandatory=$true)] [string]$BaseDir
    )

    $tempDir = Get-TempEnumerationDir -BaseDir $BaseDir
    try {
        & $BinaryPath --enumerate --out $tempDir | Out-Null
        $eventsPath = Join-Path $tempDir 'events.jsonl'
        if (-not (Test-Path -LiteralPath $eventsPath)) {
            throw "enumeration did not produce $eventsPath"
        }
        $adapters = New-Object System.Collections.ArrayList
        foreach ($line in Get-Content -LiteralPath $eventsPath -Encoding UTF8) {
            if (-not $line) { continue }
            try {
                $rec = $line | ConvertFrom-Json -ErrorAction Stop
            } catch {
                continue
            }
            if ($rec.k -ne 'ai') { continue }
            $vkIndex = $null
            foreach ($binding in @($rec.bind)) {
                if ($binding -match '^Vulkan:idx=(\d+)\b') {
                    $vkIndex = [int]$matches[1]
                    break
                }
            }
            [void]$adapters.Add([pscustomobject]@{
                adapter_id = [string]$rec.a
                luid = [uint64]$rec.luid
                pci_bdf = [string]$rec.bdf
                driver_uuid = [string]$rec.druu
                description = [string]$rec.desc
                vk_index = $vkIndex
            })
        }
        return @($adapters)
    }
    finally {
        Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Test-StableIdentityMatch {
    param(
        [Parameter(Mandatory=$true)] $Adapter,
        [Parameter(Mandatory=$true)] [string]$Identity
    )

    $id = $Identity.Trim()
    if (-not $id) { return $false }
    if ($id -match '^pci-bdf:(.+)$') { return $Adapter.pci_bdf -ieq $matches[1] }
    if ($id -match '^driver-uuid:(.+)$') { return $Adapter.driver_uuid -ieq $matches[1] }
    if ($id -match '^adapter_[0-9a-fA-F]+$') { return $Adapter.adapter_id -ieq $id }
    if ($id -match '^luid:(?:0x)?([0-9a-fA-F]+)$') {
        $hex = ('0x{0:x16}' -f [uint64]$Adapter.luid)
        return $hex -ieq ('0x' + $matches[1])
    }
    if ($id -match '^[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-9a-fA-F]$') {
        return $Adapter.pci_bdf -ieq $id
    }
    return ($Adapter.adapter_id -ieq $id) -or ($Adapter.pci_bdf -ieq $id) -or ($Adapter.driver_uuid -ieq $id)
}

function Resolve-StableAdapter {
    param(
        [Parameter(Mandatory=$true)] [object[]]$Adapters,
        [Parameter(Mandatory=$true)] [string]$Identity
    )

    foreach ($adapter in $Adapters) {
        if (Test-StableIdentityMatch -Adapter $adapter -Identity $Identity) {
            return $adapter
        }
    }
    return $null
}

function Get-SeriesCandidate {
    param(
        [Parameter(Mandatory=$true)] [object[]]$Records,
        [Parameter(Mandatory=$true)] [string]$AdapterId,
        [string[]]$MetricPreference = @(
            'gpu.adapter.vram.local.bytes_committed',
            'vram.local.current_usage_bytes',
            'vulkan.heap0.usage_bytes'
        ),
        [double]$StableThresholdBytes = 134217728.0
    )

    foreach ($metric in $MetricPreference) {
        $series = @($Records | Where-Object { $_.a -eq $AdapterId -and $_.n -eq $metric } | Sort-Object {[uint64]$_.t})
        if ($series.Count -lt 3) { continue }

        $values = @($series | ForEach-Object { [double]$_.v })
        $delta = $values[-1] - $values[0]
        $lastStep = $values[-1] - $values[-2]
        $prevStep = $values[-2] - $values[-3]
        return [pscustomobject]@{
            adapter_id = $AdapterId
            metric_name = $metric
            sample_count = $series.Count
            first_value = $values[0]
            last_value = $values[-1]
            delta_bytes = [double]$delta
            last_step_bytes = [double]$lastStep
            prev_step_bytes = [double]$prevStep
            stable = ([math]::Abs($lastStep) -le $StableThresholdBytes) -and ([math]::Abs($prevStep) -le $StableThresholdBytes)
            first_ts = [uint64]$series[0].t
            last_ts = [uint64]$series[-1].t
        }
    }

    return $null
}

function Get-ResidencyVerdict {
    param(
        [Parameter(Mandatory=$true)] [string]$EventsPath,
        [Parameter(Mandatory=$true)] [string]$TargetAdapterId,
        [Parameter(Mandatory=$true)] [string]$GamingAdapterId
    )

    $records = @(Get-JsonLines -Path $EventsPath | Where-Object { $_.k -eq 'ms' })
    $target = Get-SeriesCandidate -Records $records -AdapterId $TargetAdapterId
    $gaming = Get-SeriesCandidate -Records $records -AdapterId $GamingAdapterId

    $allCandidates = New-Object System.Collections.ArrayList
    foreach ($adapterId in @($TargetAdapterId, $GamingAdapterId)) {
        $candidate = Get-SeriesCandidate -Records $records -AdapterId $adapterId
        if ($candidate) { [void]$allCandidates.Add($candidate) }
    }

    $best = $null
    if ($allCandidates.Count -gt 0) {
        $best = @($allCandidates | Sort-Object @{Expression = { [double]$_.delta_bytes } ; Descending = $true })[0]
    }

    $minGrowthBytes = 536870912.0
    $mismatchGapBytes = 268435456.0

    $bindingStatus = 'unresolved'
    $failureReason = $null
    $evidence = 'settle-and-sample: no stable residency observed'

    if (-not $target) {
        $failureReason = "no residency samples for $TargetAdapterId"
    } elseif (-not $target.stable) {
        $failureReason = "residency did not settle on $TargetAdapterId"
        $evidence = ("settle-and-sample: {0} only {1} polls, delta {2:N1} GB" -f $target.metric_name, $target.sample_count, ($target.delta_bytes / 1GB))
    } elseif ($target.delta_bytes -lt $minGrowthBytes) {
        $failureReason = "residency delta below threshold on $TargetAdapterId"
        $evidence = ("settle-and-sample: {0} +{1:N1} GB over {2} polls on {3}" -f $target.metric_name, ($target.delta_bytes / 1GB), $target.sample_count, $TargetAdapterId)
    } elseif ($best -and ($best.adapter_id -ne $TargetAdapterId) -and ($best.delta_bytes -ge ($target.delta_bytes + $mismatchGapBytes))) {
        $bindingStatus = 'MISMATCH'
        $failureReason = "largest residency delta landed on $($best.adapter_id) instead of $TargetAdapterId"
        $evidence = ("settle-and-sample: {0} on {1} grew +{2:N1} GB over {3} polls; target {4} grew +{5:N1} GB" -f
            $best.metric_name, $best.adapter_id, ($best.delta_bytes / 1GB), $best.sample_count, $TargetAdapterId, ($target.delta_bytes / 1GB))
    } else {
        $bindingStatus = 'verified'
        $evidence = ("settle-and-sample: {0} +{1:N1} GB over {2} polls on {3}" -f $target.metric_name, ($target.delta_bytes / 1GB), $target.sample_count, $TargetAdapterId)
    }

    return [pscustomobject]@{
        binding_status = $bindingStatus
        failure_reason = $failureReason
        evidence = $evidence
        target = $target
        gaming = $gaming
        best = $best
    }
}

function Start-HiddenPowerShellScript {
    param(
        [Parameter(Mandatory=$true)] [string]$ScriptPath,
        [Parameter(Mandatory=$true)] [string[]]$Args,
        [Parameter(Mandatory=$true)] [string]$StdoutPath,
        [Parameter(Mandatory=$true)] [string]$StderrPath
    )

    $argString = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Quote-Arg $ScriptPath)) + $Args
    $proc = Start-Process -FilePath 'powershell.exe' -ArgumentList ($argString -join ' ') `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $StdoutPath -RedirectStandardError $StderrPath
    return $proc
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$slug = ($Scenario.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
if (-not $slug) { $slug = 'scenario' }
$runDir = Join-Path $RunRoot ("research-$stamp-$slug")
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$notesPath = Join-Path $runDir 'notes.md'
$manifestPath = Join-Path $runDir 'manifest.json'
$verdictPath = Join-Path $runDir 'verdict.json'
$eventsPath = Join-Path $runDir 'events.jsonl'
$hostPressurePath = Join-Path $runDir 'host-pressure.jsonl'
$dpcPath = Join-Path $runDir 'frame-impact.dpc.jsonl'
$wowLogTailPath = Join-Path $runDir 'wow-log-tail.jsonl'
$b70StdoutPath = Join-Path $runDir 'b70tools.stdout.log'
$b70StderrPath = Join-Path $runDir 'b70tools.stderr.log'
$frameCsvPathResolved = if ($FrameCsvPath) { $FrameCsvPath } elseif ($PresentMonPath) { Join-Path $runDir 'frame-impact.presentmon.csv' } else { $null }

$adapters = Get-EnumeratedAdapters -BinaryPath $B70Tools -BaseDir $runDir
$targetAdapter = Resolve-StableAdapter -Adapters $adapters -Identity $RequestedIdentity
if (-not $targetAdapter) {
    $verdict = [ordered]@{
        schema_version = 'research-verdict-v1'
        run_id = Split-Path -Leaf $runDir
        ts = (Get-Date).ToString('o')
        binding_status = 'unresolved'
        requested_identity = $RequestedIdentity
        resolved_adapter = $null
        evidence = "requested identity '$RequestedIdentity' could not be resolved at launch"
        gaming_adapter = $null
        failure_reason = "requested identity '$RequestedIdentity' could not be resolved"
    }
    Write-JsonNoBom -Path $verdictPath -Value $verdict -Depth 10
    throw "requested identity '$RequestedIdentity' could not be resolved"
}

if (-not $GamingIdentity) {
    $gamingAdapter = @($adapters | Where-Object { $_.adapter_id -ne $targetAdapter.adapter_id } | Select-Object -First 1)
} else {
    $gamingAdapter = Resolve-StableAdapter -Adapters $adapters -Identity $GamingIdentity
}
if (-not $gamingAdapter) {
    $verdict = [ordered]@{
        schema_version = 'research-verdict-v1'
        run_id = Split-Path -Leaf $runDir
        ts = (Get-Date).ToString('o')
        binding_status = 'unresolved'
        requested_identity = $RequestedIdentity
        resolved_adapter = $targetAdapter.adapter_id
        evidence = "gaming adapter could not be resolved"
        gaming_adapter = $null
        failure_reason = "gaming adapter could not be resolved"
    }
    Write-JsonNoBom -Path $verdictPath -Value $verdict -Depth 10
    throw "gaming adapter could not be resolved"
}

if ($targetAdapter.adapter_id -eq $gamingAdapter.adapter_id) {
    $verdict = [ordered]@{
        schema_version = 'research-verdict-v1'
        run_id = Split-Path -Leaf $runDir
        ts = (Get-Date).ToString('o')
        binding_status = 'MISMATCH'
        requested_identity = $RequestedIdentity
        resolved_adapter = $targetAdapter.adapter_id
        evidence = "requested identity and gaming adapter resolved to the same physical adapter"
        gaming_adapter = $gamingAdapter.adapter_id
        failure_reason = "requested identity and gaming adapter resolved to the same physical adapter"
    }
    Write-JsonNoBom -Path $verdictPath -Value $verdict -Depth 10
    throw "requested identity and gaming adapter resolved to the same physical adapter"
}

$manifest = [ordered]@{
    run_id = Split-Path -Leaf $runDir
    scenario = $Scenario
    started_at = (Get-Date).ToString('o')
    requested_identity = $RequestedIdentity
    resolved_adapter = $targetAdapter.adapter_id
    gaming_adapter = $gamingAdapter.adapter_id
    b70tools_bin = $B70Tools
    llama_server_bin = $LlamaServerBin
    model_path = $ModelPath
    alias = $Alias
    host_name = $HostName
    port = $Port
    duration_sec = $DurationSec
    frame_csv_path = $frameCsvPathResolved
    events_jsonl = $eventsPath
    host_pressure_jsonl = $hostPressurePath
    dpc_jsonl = $dpcPath
    wow_log_tail_jsonl = if ($TailWowLog) { $wowLogTailPath } else { $null }
    b70tools_stdout = $b70StdoutPath
    b70tools_stderr = $b70StderrPath
    notes_path = $notesPath
    verdict_path = $verdictPath
    status = 'pending'
}
Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10

Write-TextNoBom -Path $notesPath -Content @"
# $Scenario

Run: $($manifest.run_id)
Started: $($manifest.started_at)
Requested identity: $RequestedIdentity
Resolved adapter: $($targetAdapter.adapter_id)
Gaming adapter: $($gamingAdapter.adapter_id)

"@

Write-Output "[run] $runDir"
Write-Output "[bind] requested=$RequestedIdentity resolved=$($targetAdapter.adapter_id) gaming=$($gamingAdapter.adapter_id)"

if ($DryRun) {
    # Resolution-only verification: enumerate + stable-identity resolve, then stop.
    # No telemetry capture, no llama-server, no stimulus, no verdict — hazard-safe.
    $dryReport = [ordered]@{
        mode = 'dry-run'
        run_id = $manifest.run_id
        ts = (Get-Date).ToString('o')
        requested_identity = $RequestedIdentity
        resolved_adapter = $targetAdapter.adapter_id
        resolved_vk_index = $targetAdapter.vk_index
        gaming_adapter = $gamingAdapter.adapter_id
        would_bind = "GGML_VK_VISIBLE_DEVICES=$($targetAdapter.vk_index) -> $($targetAdapter.adapter_id) ($($targetAdapter.pci_bdf))"
        adapters = @($adapters | ForEach-Object { [ordered]@{ adapter_id = $_.adapter_id; pci_bdf = $_.pci_bdf; driver_uuid = $_.driver_uuid; vk_index = $_.vk_index; description = $_.description } })
        note = 'Resolution-only verification. No inference launched, no telemetry captured, no verdict written.'
    }
    $dryReportPath = Join-Path $runDir 'dry-run-report.json'
    Write-JsonNoBom -Path $dryReportPath -Value $dryReport -Depth 10
    $manifest.status = 'dry-run'
    Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10
    Write-Output "[dry-run] would bind vk:$($targetAdapter.vk_index) -> $($targetAdapter.adapter_id) (gaming=$($gamingAdapter.adapter_id))"
    Write-Output "[dry-run] report: $dryReportPath"
    exit 0
}

$hostProc = Start-HiddenPowerShellScript -ScriptPath $watchHostScript -Args @(
    '-OutPath', (Quote-Arg $hostPressurePath),
    '-IntervalSec', '1',
    '-ProcessNames', (Quote-Arg ($HostProcessNames -join ','))
) -StdoutPath (Join-Path $runDir 'watch-host.stdout.log') -StderrPath (Join-Path $runDir 'watch-host.stderr.log')
$manifest.host_pressure_pid = $hostProc.Id
Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10

$dpcProc = Start-HiddenPowerShellScript -ScriptPath $watchDpcScript -Args @(
    '-OutPath', (Quote-Arg $dpcPath),
    '-IntervalSec', "$DpcIntervalSec"
) -StdoutPath (Join-Path $runDir 'watch-dpc.stdout.log') -StderrPath (Join-Path $runDir 'watch-dpc.stderr.log')
$manifest.dpc_pid = $dpcProc.Id
Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10

$logTailProc = $null
if ($TailWowLog) {
    if (-not (Test-Path -LiteralPath $WowLogPath)) { throw "WoW combat log not found: $WowLogPath" }
    $logTailProc = Start-HiddenPowerShellScript -ScriptPath $watchLogScript -Args @(
        '-LogPath', (Quote-Arg $WowLogPath),
        '-OutPath', (Quote-Arg $wowLogTailPath),
        '-IntervalSec', "$WowLogTailIntervalSec"
    ) -StdoutPath (Join-Path $runDir 'watch-wowlog.stdout.log') -StderrPath (Join-Path $runDir 'watch-wowlog.stderr.log')
    $manifest.wow_log_tail_pid = $logTailProc.Id
    Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10
}

$presentProc = $null
if ($PresentMonPath) {
    if (-not (Test-Path -LiteralPath $PresentMonPath)) { throw "PresentMon not found: $PresentMonPath" }
    $presentProc = Start-Process -FilePath $PresentMonPath -ArgumentList $PresentMonArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $runDir 'frame-impact.presentmon.stdout.log') `
        -RedirectStandardError (Join-Path $runDir 'frame-impact.presentmon.stderr.log')
    $manifest.presentmon_pid = $presentProc.Id
    $manifest.frame_csv_path = $frameCsvPathResolved
    Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10
}

Write-Output "[b70tools] starting telemetry capture"
$b70Proc = Start-Process -FilePath $B70Tools -ArgumentList @('run', '--ticks', "$DurationSec", '--slow-cadence-ms', '15000', '--out', $runDir) `
    -PassThru -WindowStyle Hidden -RedirectStandardOutput $b70StdoutPath -RedirectStandardError $b70StderrPath
$manifest.b70tools_pid = $b70Proc.Id
Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10

$serverWrapperArgs = @{
    RunDir = $runDir
    LlamaServerBin = $LlamaServerBin
    ModelPath = $ModelPath
    VisibleDevices = [string]$targetAdapter.vk_index
    RequestedIdentity = $RequestedIdentity
    ResolvedAdapterId = $targetAdapter.adapter_id
    HostName = $HostName
    Port = $Port
    Alias = $Alias
}
if ($null -ne $GamingIdentity -and $GamingIdentity) {
    $serverWrapperArgs.AllowMultiGpu = $true
}

try {
    & $serverScript @serverWrapperArgs
    $serverManifestPath = Join-Path $runDir 'llama-server.manifest.json'
    if (Test-Path -LiteralPath $serverManifestPath) {
        $serverManifest = Get-Content -LiteralPath $serverManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $manifest.server_pid = $serverManifest.pid
        $manifest.server_manifest_path = $serverManifestPath
        $manifest.server_stdout = $serverManifest.stdout_path
        $manifest.server_stderr = $serverManifest.stderr_path
        Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10
    }

    Write-Output "[inference] sending bounded server stimulus"
    & $stimulusScript `
        -RunDir $runDir `
        -Mode Server `
        -BaseUrl ("http://$HostName`:$Port") `
        -Label 'research-gauge-launch' `
        -PromptText 'Return exactly 24 numbered lines. Each line should be one short operational check for a live GPU inference benchmark. Keep each line concise, but do not collapse them into paragraphs.' `
        -MaxTokens 512 `
        -Temperature 0.1 `
        -BindingIdentity $RequestedIdentity
}
catch {
    $serverFailure = $_.Exception.Message
    $manifest.server_error = $serverFailure
    Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10
}

try {
    $timeoutMs = [Math]::Max(60000, ($DurationSec + 90) * 1000)
    if (-not $b70Proc.WaitForExit($timeoutMs)) {
        throw "b70tools did not exit after timeout; telemetry process id $($b70Proc.Id)"
    }
    $manifest.b70tools_exit_code = $b70Proc.ExitCode
    Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10
}
finally {
    if ($presentProc -and -not $presentProc.HasExited) {
        try { $presentProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    if ($logTailProc -and -not $logTailProc.HasExited) {
        try { $logTailProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    if ($hostProc -and -not $hostProc.HasExited) {
        try { $hostProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    if ($dpcProc -and -not $dpcProc.HasExited) {
        try { $dpcProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    if ($manifest.server_pid) {
        try { Stop-Process -Id $manifest.server_pid -Force -ErrorAction SilentlyContinue } catch {}
    }
}

Start-Sleep -Seconds 1
& $summarizeScript -RunDir $runDir

$residency = Get-ResidencyVerdict -EventsPath $eventsPath -TargetAdapterId $targetAdapter.adapter_id -GamingAdapterId $gamingAdapter.adapter_id

$verdict = [ordered]@{
    schema_version = 'research-verdict-v1'
    run_id = Split-Path -Leaf $runDir
    ts = (Get-Date).ToString('o')
    binding_status = $residency.binding_status
    requested_identity = $RequestedIdentity
    resolved_adapter = $targetAdapter.adapter_id
    evidence = $residency.evidence
    gaming_adapter = $gamingAdapter.adapter_id
    failure_reason = $residency.failure_reason
}
Write-JsonNoBom -Path $verdictPath -Value $verdict -Depth 10
if (-not (Test-Utf8NoBom -Path $verdictPath)) { throw "verdict.json is not UTF-8 no-BOM: $verdictPath" }

$manifest.finished_at = (Get-Date).ToString('o')
$manifest.binding_status = $residency.binding_status
$manifest.binding_evidence = $residency.evidence
$manifest.verdict_path = $verdictPath
$manifest.status = 'complete'
Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10

if ($residency.binding_status -ne 'verified') {
    Write-Output "[verdict] $($residency.binding_status) - $($residency.failure_reason)"
    exit 3
}

Write-Output "[verdict] verified"
Write-Output "[done] $runDir"
