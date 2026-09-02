[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$ProjectPath,
    [Parameter(Mandatory=$true)] [string]$GodotPath,
    [ValidateSet('d3d12', 'vulkan')] [string]$Renderer = 'd3d12',
    [ValidateSet('feel', 'default', 'stress')] [string]$Preset = 'default',
    [int]$TreeCount = 0,
    [int]$WarmupSeconds = 10,
    [int]$CaptureSeconds = 60,
    [int]$GpuIndex = -1,
    [string]$Resolution = '1920x1080',
    [string]$ScenarioPath = '',
    [string]$ExpectedAdapterId = '',
    [string]$B70ToolsPath = '',
    [string]$PresentMonPath = '',
    [string[]]$PresentMonArgs = @(),
    [string]$RunRoot = '',
    [string]$RunLabel = '',
    [switch]$Screenshot,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
. (Join-Path $repoRoot 'scripts\wow-impact\NoBom.ps1')

$ProjectPath = [IO.Path]::GetFullPath($ProjectPath)
$GodotPath = [IO.Path]::GetFullPath($GodotPath)
if (-not $B70ToolsPath) { $B70ToolsPath = Join-Path $repoRoot 'build\b70tools.exe' }
$B70ToolsPath = [IO.Path]::GetFullPath($B70ToolsPath)
if (-not $RunRoot) { $RunRoot = Join-Path $repoRoot 'runs' }
$RunRoot = [IO.Path]::GetFullPath($RunRoot)

if (-not (Test-Path -LiteralPath (Join-Path $ProjectPath 'project.godot'))) { throw "Godot project not found: $ProjectPath" }
if (-not (Test-Path -LiteralPath $GodotPath)) { throw "Godot executable not found: $GodotPath" }
if (-not (Test-Path -LiteralPath $B70ToolsPath)) { throw "b70tools executable not found: $B70ToolsPath" }
if ($PresentMonPath -and -not (Test-Path -LiteralPath $PresentMonPath)) { throw "PresentMon not found: $PresentMonPath" }
if ($Resolution -notmatch '^[1-9][0-9]{2,4}x[1-9][0-9]{2,4}$') { throw "Resolution must look like 1920x1080." }
if ($TreeCount -lt 0 -or $TreeCount -gt 65536) { throw "TreeCount must be in [0, 65536]." }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$density = if ($TreeCount -gt 0) { "$TreeCount" } else { $Preset }
$suffix = if ($RunLabel) { '-' + (($RunLabel.ToLowerInvariant() -replace '[^a-z0-9]+','-').Trim('-')) } else { '' }
$runDir = Join-Path $RunRoot "godot-forest-$stamp-$Renderer-$density$suffix"
$receiptPath = Join-Path $runDir 'forest-benchmark.json'
$screenshotPath = Join-Path $runDir 'forest-screenshot.png'
$godotLogPath = Join-Path $runDir 'godot.log'
$hostPressurePath = Join-Path $runDir 'host-pressure.jsonl'
$enumerationDir = Join-Path $runDir 'adapter-enumeration'
$frameCsvPath = Join-Path $runDir 'frame-times.csv'

$sourceRevision = (& git -C $ProjectPath rev-parse HEAD 2>$null | Out-String).Trim()
$sourceDirty = if ((& git -C $ProjectPath status --porcelain 2>$null | Out-String).Trim()) { 'true' } else { 'false' }

$plan = [ordered]@{
    schema = 'b70tools.godot-forest-run/v1'
    run_dir = $runDir
    renderer = $Renderer
    preset = $Preset
    tree_count_override = if ($TreeCount -gt 0) { $TreeCount } else { $null }
    warmup_seconds = $WarmupSeconds
    capture_seconds = $CaptureSeconds
    gpu_index = if ($GpuIndex -ge 0) { $GpuIndex } else { $null }
    resolution = $Resolution
    expected_adapter_id = if ($ExpectedAdapterId) { $ExpectedAdapterId } else { $null }
    project_path = $ProjectPath
    godot_path = $GodotPath
    b70tools_path = $B70ToolsPath
    source_revision = if ($sourceRevision) { $sourceRevision } else { 'unknown' }
    source_dirty = $sourceDirty
    scenario_path = if ($ScenarioPath) { [IO.Path]::GetFullPath($ScenarioPath) } else { $null }
    presentmon_path = if ($PresentMonPath) { [IO.Path]::GetFullPath($PresentMonPath) } else { $null }
}

if ($DryRun) {
    $plan | ConvertTo-Json -Depth 8
    exit 0
}

New-Item -ItemType Directory -Force -Path $runDir | Out-Null
Write-JsonNoBom -Path (Join-Path $runDir 'run-manifest.json') -Value $plan -Depth 10

& $B70ToolsPath --enumerate --out $enumerationDir *> (Join-Path $runDir 'adapter-enumeration.txt')
if ($LASTEXITCODE -ne 0) { throw "b70tools adapter enumeration failed with code $LASTEXITCODE" }
$adapters = @(Get-Content -LiteralPath (Join-Path $enumerationDir 'events.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
if ($ExpectedAdapterId -and -not ($adapters | Where-Object { $_.a -eq $ExpectedAdapterId })) {
    throw "Expected adapter $ExpectedAdapterId was not enumerated. Refusing to benchmark the wrong device."
}

$hostWatcher = Join-Path $repoRoot 'scripts\wow-impact\Watch-HostPressure.ps1'
$hostArgs = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$hostWatcher,'-OutPath',$hostPressurePath,'-IntervalSec','1','-ProcessNames','Godot,Godot_v4.6.1-stable_mono_win64,Godot_v4.6.1-stable_mono_win64_console')
$hostProcess = Start-Process -FilePath 'powershell.exe' -ArgumentList $hostArgs -PassThru -WindowStyle Hidden

$telemetryTicks = [Math]::Max(20, $WarmupSeconds + $CaptureSeconds + 30)
$telemetryArgs = @('--run','--ticks',"$telemetryTicks",'--flush-every-tick','--out',$runDir)
$telemetryProcess = Start-Process -FilePath $B70ToolsPath -ArgumentList $telemetryArgs -PassThru -WindowStyle Hidden

$presentProcess = $null
if ($PresentMonPath) {
    $resolvedPresentArgs = @($PresentMonArgs | ForEach-Object { $_.Replace('{csv}', $frameCsvPath).Replace('{process}', 'Godot') })
    $presentProcess = Start-Process -FilePath $PresentMonPath -ArgumentList $resolvedPresentArgs -PassThru -WindowStyle Hidden
}

$engineArgs = @('--path',$ProjectPath,'--rendering-driver',$Renderer,'--resolution',$Resolution,'--disable-vsync')
if ($GpuIndex -ge 0) { $engineArgs += @('--gpu-index',"$GpuIndex") }
$userArgs = @('--lab=forest-storm',"--forest-preset=$Preset", "--warmup-seconds=$WarmupSeconds", "--capture-seconds=$CaptureSeconds", "--receipt=$receiptPath")
if ($TreeCount -gt 0) { $userArgs += "--tree-count=$TreeCount" }
if ($ScenarioPath) { $userArgs += "--scenario=$([IO.Path]::GetFullPath($ScenarioPath))" }
if ($Screenshot) { $userArgs += "--screenshot=$screenshotPath" }

$oldRevision = $env:LUMBERJACKS_SOURCE_REVISION
$oldDirty = $env:LUMBERJACKS_SOURCE_DIRTY
$env:LUMBERJACKS_SOURCE_REVISION = if ($sourceRevision) { $sourceRevision } else { 'unknown' }
$env:LUMBERJACKS_SOURCE_DIRTY = $sourceDirty
$exitCode = -1
try {
    Write-Output "[run] $runDir"
    Write-Output "[godot] renderer=$Renderer density=$density resolution=$Resolution gpu_index=$GpuIndex"
    & $GodotPath @engineArgs -- @userArgs 2>&1 | Tee-Object -FilePath $godotLogPath
    $exitCode = $LASTEXITCODE
}
finally {
    $env:LUMBERJACKS_SOURCE_REVISION = $oldRevision
    $env:LUMBERJACKS_SOURCE_DIRTY = $oldDirty
    foreach ($process in @($presentProcess, $telemetryProcess, $hostProcess)) {
        if ($process -and -not $process.HasExited) {
            try { $process | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
        }
    }
}

$plan.godot_exit_code = $exitCode
$plan.finished_utc = (Get-Date).ToUniversalTime().ToString('o')
$plan.adapter_inventory = @($adapters | ForEach-Object { [ordered]@{ adapter_id=$_.a; description=$_.desc; pci_bdf=$_.bdf; driver_uuid=$_.druu; bindings=$_.bind } })
$plan.receipt = if (Test-Path -LiteralPath $receiptPath) { Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json } else { $null }

# Two identical B70 names make a Godot adapter string insufficient. Bind the rendered adapter to
# b70tools' stable LUID identity by selecting the matching adapter with the largest observed
# render/compute counter delta during this run.
$runEventsPath = Join-Path $runDir 'events.jsonl'
$activity = @()
if (Test-Path -LiteralPath $runEventsPath) {
    $activityEvents = @(Get-Content -LiteralPath $runEventsPath | ForEach-Object {
        $event = $_ | ConvertFrom-Json
        if ($event.k -eq 'ms' -and $event.n -eq 'gpu.activity.render_compute_counter') { $event }
    })
    $activity = @($activityEvents | Group-Object a | ForEach-Object {
        $samples = @($_.Group | Sort-Object t)
        if ($samples.Count -ge 2) {
            [pscustomobject]@{
                adapter_id = $_.Name
                counter_delta_ns = [Math]::Max(0.0, [double]$samples[-1].v - [double]$samples[0].v)
                wall_delta_ns = [Math]::Max(1.0, [double]$samples[-1].t - [double]$samples[0].t)
                sample_count = $samples.Count
            }
        }
    })
}
$receiptAdapter = $plan.receipt.adapter
$matchingIds = @($adapters | Where-Object { $_.desc -eq $receiptAdapter } | ForEach-Object a)
$activeAdapter = $activity |
    Where-Object { $_.adapter_id -in $matchingIds } |
    Sort-Object counter_delta_ns -Descending |
    Select-Object -First 1
$plan.adapter_activity = @($activity | Sort-Object counter_delta_ns -Descending)
$plan.observed_active_adapter_id = $activeAdapter.adapter_id
Write-JsonNoBom -Path (Join-Path $runDir 'run-manifest.json') -Value $plan -Depth 15

foreach ($analysis in @('adapters','summarize','disagreements','self')) {
    & $B70ToolsPath $analysis $runDir *> (Join-Path $runDir "b70tools-$analysis.txt")
}

if ($exitCode -ne 0) { throw "Godot exited with code $exitCode. See $godotLogPath" }
if (-not (Test-Path -LiteralPath $receiptPath)) { throw "Godot exited without writing $receiptPath" }
if ($ExpectedAdapterId -and $activeAdapter.adapter_id -ne $ExpectedAdapterId) {
    throw "Godot rendered on '$($activeAdapter.adapter_id)', not expected adapter '$ExpectedAdapterId'. See run-manifest.json."
}
Write-Output "[receipt] $receiptPath"
if ($activeAdapter) { Write-Output "[adapter] $($activeAdapter.adapter_id) ($receiptAdapter)" }
Write-Output "[next] Compare repeated D3D12/Vulkan runs with Compare-GodotForestRuns.ps1 -RunRoot '$RunRoot'"
