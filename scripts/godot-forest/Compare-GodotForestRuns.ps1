[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)] [string]$RunRoot,
    [string]$OutputPath = ''
)

$ErrorActionPreference = 'Stop'
$scriptRoot = $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
. (Join-Path $repoRoot 'scripts\wow-impact\NoBom.ps1')
$RunRoot = [IO.Path]::GetFullPath($RunRoot)
if (-not (Test-Path -LiteralPath $RunRoot)) { throw "Run root not found: $RunRoot" }
if (-not $OutputPath) { $OutputPath = Join-Path $RunRoot 'forest-renderer-comparison.json' }

$receipts = @(Get-ChildItem -LiteralPath $RunRoot -Recurse -Filter 'forest-benchmark.json' | ForEach-Object {
    $value = Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
    [pscustomobject]@{ path=$_.FullName; value=$value }
})
if ($receipts.Count -lt 2) { throw "At least two forest benchmark receipts are required." }

$identity = $receipts[0].value
$mismatch = @($receipts | Where-Object {
    $_.value.schema -ne 'lumberjacks.forest-benchmark/v1' -or
    $_.value.asset_set_sha256 -ne $identity.asset_set_sha256 -or
    $_.value.scenario_sha256 -ne $identity.scenario_sha256 -or
    $_.value.placement_sha256 -ne $identity.placement_sha256 -or
    $_.value.tree_count -ne $identity.tree_count -or
    ($_.value.viewport -join 'x') -ne ($identity.viewport -join 'x')
})
if ($mismatch.Count -gt 0) {
    throw "Receipts do not describe the same assets, scenario, placement, density, and viewport: $($mismatch.path -join ', ')"
}

function Get-Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    if ($sorted.Count % 2) { return [double]$sorted[[int][Math]::Floor($sorted.Count / 2)] }
    return ([double]$sorted[$sorted.Count / 2 - 1] + [double]$sorted[$sorted.Count / 2]) / 2.0
}

$groups = @($receipts | Group-Object { $_.value.rendering_driver } | ForEach-Object {
    $items = @($_.Group)
    [pscustomobject]@{
        renderer = $_.Name
        runs = $items.Count
        median_p50_ms = [Math]::Round((Get-Median @($items.value.p50_ms)), 3)
        median_p99_ms = [Math]::Round((Get-Median @($items.value.p99_ms)), 3)
        max_ms = [Math]::Round((@($items.value.max_ms) | Measure-Object -Maximum).Maximum, 3)
        over_50ms = (@($items.value.over_50ms) | Measure-Object -Sum).Sum
        paths = @($items.path)
    }
})
if (-not ($groups | Where-Object renderer -eq 'd3d12') -or -not ($groups | Where-Object renderer -eq 'vulkan')) {
    throw "Both d3d12 and vulkan receipts are required."
}

$d3d12 = $groups | Where-Object renderer -eq 'd3d12'
$vulkan = $groups | Where-Object renderer -eq 'vulkan'
$difference = [Math]::Abs($d3d12.median_p99_ms - $vulkan.median_p99_ms)
$relative = if ([Math]::Max($d3d12.median_p99_ms, $vulkan.median_p99_ms) -gt 0) {
    $difference / [Math]::Max($d3d12.median_p99_ms, $vulkan.median_p99_ms)
} else { 0 }
$winner = if ($relative -le 0.05) { 'd3d12' } elseif ($d3d12.median_p99_ms -lt $vulkan.median_p99_ms) { 'd3d12' } else { 'vulkan' }
$reason = if ($relative -le 0.05) { 'p99 results are within 5%; retain the existing D3D12 default' } else { 'lower median p99 frame time' }

$comparison = [ordered]@{
    schema = 'b70tools.godot-forest-comparison/v1'
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    tree_count = $identity.tree_count
    viewport = $identity.viewport
    scenario_sha256 = $identity.scenario_sha256
    placement_sha256 = $identity.placement_sha256
    asset_set_sha256 = $identity.asset_set_sha256
    groups = $groups
    selected_renderer = $winner
    selection_reason = $reason
    relative_p99_difference = [Math]::Round($relative, 5)
}
Write-JsonNoBom -Path $OutputPath -Value $comparison -Depth 12
$comparison | ConvertTo-Json -Depth 12
Write-Output "[comparison] $OutputPath"
