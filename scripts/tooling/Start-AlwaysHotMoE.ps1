# Start-AlwaysHotMoE.ps1
#
# Keeps Qwen3-30B-A3B MoE hot on a SINGLE B70 as a persistent OpenAI-compatible endpoint,
# for offloading simple/tooling/MCP work off cloud models. MoE was the throughput winner on
# this rig (best prefill AND decode: ~517 / ~11.8 tok/s on the 20260605 sweep) and the least
# bandwidth-bound, so it's the best citizen on a busy/contended box.
#
# This is a thin SUPERVISOR over scripts/wow-impact/Start-SecondB70LlamaServer.ps1 (the proven
# single-card launcher): it (re)launches on crash/unhealthy and blocks, logging each event.
# llama-server natively serves the OpenAI API at  http://<host>:<port>/v1  (chat/completions,
# models) -- point MCP servers / tools / leopard (AskProviderApi="openai") at that base URL.
#
# DO NOT run this at the same time as the dual-card stress sweep (Start-StressSuite uses BOTH
# cards on :8080) -- the single card would contend. Start it once the sweep is done, or on a
# card the sweep isn't using.
#
# Usage:
#   & 'D:\work\b70tools\scripts\tooling\Start-AlwaysHotMoE.ps1'
#   & '...\Start-AlwaysHotMoE.ps1' -VisibleDevices 0 -Port 8090 -Context 32768
#   Ctrl+C to stop supervising (also stops the child server).

param(
    [string]$ModelPath      = 'D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf',
    [string]$VisibleDevices = '1',          # single Vulkan index; the card the tooling model lives on
    [int]   $Port           = 8090,         # dedicated tooling port (8080 = live loop / stress sweep)
    [int]   $Context        = 16384,
    [int]   $ParallelSlots  = 1,
    [string]$Alias          = 'b70-moe',    # the OpenAI "model" name clients reference
    [int]   $PollSec        = 15,
    [int]   $HealthTimeoutSec = 240,
    [string]$Launcher       = 'D:\work\b70tools\scripts\wow-impact\Start-SecondB70LlamaServer.ps1',
    [string]$LogDir         = 'D:\work\b70tools\runs\always-hot-moe',
    [int]   $MaxRestarts    = 0              # 0 = unlimited
)

$ErrorActionPreference = 'Continue'
$HostName = '127.0.0.1'
$url = "http://$HostName`:$Port"
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$superLog = Join-Path $LogDir 'supervisor.log'

function Log([string]$m) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $m
    Write-Host $line
    Add-Content -Path $superLog -Value $line -Encoding utf8
}
function Test-Healthy {
    try { return ((Invoke-WebRequest -Uri "$url/health" -UseBasicParsing -TimeoutSec 4).StatusCode -lt 300) }
    catch { return $false }
}

if (-not (Test-Path -LiteralPath $ModelPath)) { Log "FATAL: model not found: $ModelPath"; exit 2 }
if (-not (Test-Path -LiteralPath $Launcher))  { Log "FATAL: launcher not found: $Launcher"; exit 2 }

Log "always-hot MoE supervisor starting"
Log "model=$ModelPath  card(vk)=$VisibleDevices  endpoint=$url/v1  alias=$Alias  ctx=$Context"

$childPid = $null
$restarts = 0

# Stop the child cleanly on Ctrl+C / exit.
$stopChild = { if ($script:childPid) { try { Stop-Process -Id $script:childPid -Force -ErrorAction SilentlyContinue } catch {} } }
Register-EngineEvent -SourceIdentifier PowerShell.Exiting -Action $stopChild | Out-Null

try {
    while ($true) {
        if (Test-Healthy) { Start-Sleep -Seconds $PollSec; continue }

        Log "endpoint not healthy -- (re)launching"
        # Kill a hung child still holding the port, so the launcher's port-open guard won't throw.
        if ($childPid) { try { Stop-Process -Id $childPid -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 2 } catch {} }

        try {
            $out = & $Launcher `
                -ModelPath $ModelPath -VisibleDevices $VisibleDevices `
                -HostName $HostName -Port $Port -Context $Context `
                -ParallelSlots $ParallelSlots -Alias $Alias `
                -HealthTimeoutSec $HealthTimeoutSec 2>&1
            $m = [regex]::Match(($out -join "`n"), 'pid=(\d+)')
            $childPid = if ($m.Success) { [int]$m.Groups[1].Value } else { $null }
            $mm = [regex]::Match(($out -join "`n"), 'manifest=(.+)')
            $manifestPath = if ($mm.Success) { $mm.Groups[1].Value.Trim() } else { '?' }
        }
        catch {
            Log "launch FAILED: $($_.Exception.Message) -- retry in 10s"
            Start-Sleep -Seconds 10
            continue
        }

        if (Test-Healthy) {
            $restarts++
            Log "MoE HOT (pid=$childPid restart#$restarts)  $url/v1  model='$Alias'  manifest=$manifestPath"
        } else {
            Log "launched (pid=$childPid) but not healthy yet within ${HealthTimeoutSec}s; will recheck"
        }

        if ($MaxRestarts -gt 0 -and $restarts -ge $MaxRestarts) {
            Log "MaxRestarts ($MaxRestarts) reached -- supervisor exiting (child left running)."
            break
        }
        Start-Sleep -Seconds $PollSec
    }
}
finally {
    & $stopChild
    Log "supervisor stopped."
}
