param(
    [string]$LlamaServerBin = 'D:\work\battlemage\llamacpp-win-vulkan\llama-server.exe',
    [string]$ModelPath = 'D:\work\battlemage\models\qwen2.5-14b-instruct-q4_K_M.gguf',
    [string]$VisibleDevices = '1',
    [string]$RequestedIdentity = '',
    [string]$ResolvedAdapterId = '',
    [string]$HostName = '127.0.0.1',
    [int]$Port = 8080,
    [string]$Alias = 'tempo-b70-second',
    [int]$Context = 4096,
    [int]$ParallelSlots = 1,
    [int]$GpuLayers = 99,
    [string]$RunRoot = 'D:\work\b70tools\runs',
    [string]$RunDir = '',
    [int]$HealthTimeoutSec = 180,
    [switch]$AllowMultiGpu,
    [switch]$NoWait
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NoBom.ps1')

function Quote-Arg {
    param([string]$Value)
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Test-LocalPortOpen {
    param(
        [string]$HostName,
        [int]$Port
    )

    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $iar = $client.BeginConnect($HostName, $Port, $null, $null)
        if (-not $iar.AsyncWaitHandle.WaitOne(250)) { return $false }
        $client.EndConnect($iar)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $client.Close()
    }
}

$VisibleDevices = $VisibleDevices.Trim()
if (-not $VisibleDevices) { throw 'VisibleDevices cannot be empty.' }

$singleDevicePattern = '^\d+$'
if (-not $AllowMultiGpu -and $VisibleDevices -notmatch $singleDevicePattern) {
    throw "Refusing multi-GPU visible devices '$VisibleDevices'. Use a single Vulkan index such as '1', or pass -AllowMultiGpu for an intentional experiment."
}

if (-not (Test-Path -LiteralPath $LlamaServerBin)) { throw "llama-server not found: $LlamaServerBin" }
if (-not (Test-Path -LiteralPath $ModelPath)) { throw "model not found: $ModelPath" }
if (Test-LocalPortOpen -HostName $HostName -Port $Port) { throw "Port $HostName`:$Port is already open. Stop the existing server or choose another -Port." }

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = if ($RunDir) { $RunDir } else { Join-Path $RunRoot "llama-server-second-b70-$stamp" }
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$stdoutPath = Join-Path $runDir 'llama-server.stdout.log'
$stderrPath = Join-Path $runDir 'llama-server.stderr.log'
$manifestPath = Join-Path $runDir 'llama-server.manifest.json'
$tempoSettingsPath = Join-Path $runDir 'llama-server.tempo-settings-snippet.json'
$url = "http://$HostName`:$Port"

$serverArgs = @(
    '-m', $ModelPath,
    '-ngl', "$GpuLayers",
    '--no-mmap',
    '-dio',
    '-c', "$Context",
    '-np', "$ParallelSlots",
    '--host', $HostName,
    '--port', "$Port",
    '--metrics',
    '--alias', $Alias
)

$quotedArgs = $serverArgs | ForEach-Object {
    if ($_ -match '[\s"]') { Quote-Arg $_ } else { $_ }
}
$processArgString = $quotedArgs -join ' '

$oldVisible = $env:GGML_VK_VISIBLE_DEVICES
$oldCoop = $env:GGML_VK_DISABLE_COOPMAT
$env:GGML_VK_VISIBLE_DEVICES = $VisibleDevices
$env:GGML_VK_DISABLE_COOPMAT = '1'

try {
    $proc = Start-Process -FilePath $LlamaServerBin `
        -ArgumentList $processArgString `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru `
        -WindowStyle Hidden
}
finally {
    $env:GGML_VK_VISIBLE_DEVICES = $oldVisible
    $env:GGML_VK_DISABLE_COOPMAT = $oldCoop
}

$tempoSettings = [ordered]@{
    llmProvider = 'LocalLlamaServer'
    lanternBackend = 'LlamaServer'
    llamaServerBaseUrl = $url
    llamaServerModel = $Alias
}
Write-JsonNoBom -Path $tempoSettingsPath -Value $tempoSettings -Depth 8

$manifest = [ordered]@{
    scenario = 'second-b70-llama-server'
    started_at = (Get-Date).ToString('o')
    pid = $proc.Id
    url = $url
    alias = $Alias
    llama_server_bin = $LlamaServerBin
    model_path = $ModelPath
    requested_identity = $RequestedIdentity
    resolved_adapter = $ResolvedAdapterId
    binding_status = if ($RequestedIdentity -and $ResolvedAdapterId) { 'verified' } else { 'unresolved' }
    allow_multi_gpu = [bool]$AllowMultiGpu
    llama_args = $serverArgs
    command_line = (Quote-Arg $LlamaServerBin) + ' ' + ($quotedArgs -join ' ')
    stdout_path = $stdoutPath
    stderr_path = $stderrPath
    tempo_settings_snippet_path = $tempoSettingsPath
    notes = @(
        'Tempo does not choose the GPU directly; it talks to this HTTP server.',
        'This launch binds one Vulkan device at launch time using a stable physical identity and never persists the Vulkan index.',
        'For live WoW testing, do not use multi-GPU layer-split flags.'
    )
}
Write-JsonNoBom -Path $manifestPath -Value $manifest -Depth 10

if (-not $NoWait) {
    $deadline = (Get-Date).AddSeconds($HealthTimeoutSec)
    $healthy = $false
    do {
        if ($proc.HasExited) {
            throw "llama-server exited early with code $($proc.ExitCode). See $stderrPath"
        }

        try {
            $response = Invoke-WebRequest -Uri "$url/health" -UseBasicParsing -TimeoutSec 3
            if ($response.StatusCode -ge 200 -and $response.StatusCode -lt 300) {
                $healthy = $true
                break
            }
        }
        catch {
            Start-Sleep -Seconds 2
        }
    } while ((Get-Date) -lt $deadline)

    if (-not $healthy) {
        Write-Warning "llama-server did not report healthy within $HealthTimeoutSec seconds. PID $($proc.Id) is still running; inspect $stderrPath"
    }
}

Write-Output "[llama-server] pid=$($proc.Id)"
Write-Output "[llama-server] url=$url alias=$Alias"
Write-Output "[llama-server] binding=stable-identity requested=$RequestedIdentity resolved=$ResolvedAdapterId"
Write-Output "[llama-server] manifest=$manifestPath"
Write-Output "[llama-server] tempo_settings=$tempoSettingsPath"
Write-Output '[llama-server] Tempo settings snippet:'
Get-Content -LiteralPath $tempoSettingsPath
