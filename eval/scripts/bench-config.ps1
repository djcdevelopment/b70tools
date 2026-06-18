# bench-config.ps1
#
# Single-config micro-benchmark for dialing in dual-B70 inference speed.
# Loads llama-server with ONE explicit config, fires ONE long-context prompt
# with a short n_predict, reads prefill + decode t/s from the response timings,
# then tears the server down. No telemetry, no verdict gate, no multi-round —
# just the speed number, so config sweeps iterate in minutes.
#
# Each invocation is independent and self-cleaning (kills stray llama-server
# first, stops its own server in finally).
#
# Usage:
#   .\bench-config.ps1 -Label "baseline"            # dual, layer, q8, coopmat OFF
#   .\bench-config.ps1 -Label "coopmat-on" -Coopmat
#   .\bench-config.ps1 -Label "single"  -Devices 0
#   .\bench-config.ps1 -Label "single+coop" -Devices 0 -Coopmat -KvType f16

param(
    [Parameter(Mandatory=$true)] [string]$Label,
    [string]$ModelPath   = 'D:\work\battlemage\models\qwen2.5-32b-instruct-q4_K_M.gguf',
    # Context to inject. Default = a real ~70k-byte (~26k-token) eval prompt so we
    # measure decode at the painful long-context depth, not an empty cache.
    [string]$ContextFile = 'D:\work\b70tools\eval\runs\qwen2.5-32b-q4-stress-20260618-032155\round-1\prompt-1-injected-context.md',
    [string]$Devices     = '0,1',     # '0,1' = dual; '0' or '1' = single card
    [string]$Sm          = 'layer',   # split mode for dual: layer | row  (ignored when single)
    [string]$Ts          = '1,1',
    [switch]$Coopmat,                 # present => coopmat ENABLED (GGML_VK_DISABLE_COOPMAT=0)
    [string]$KvType      = 'q8_0',    # q8_0 | f16
    [string]$Fa          = 'on',      # flash attention: on | off
    [switch]$Tiny,                    # use a tiny prompt (near-empty context) to isolate attention cost
    [int]   $Ctx         = 32768,
    [int]   $NPredict    = 200,       # short: enough to get a clean decode t/s fast
    [int]   $Port        = 8081,      # 8081 to avoid colliding with always-hot :8090 / eval :8080
    [string]$LlamaServerBin = 'D:\work\battlemage\llamacpp-win-vulkan\llama-server.exe',
    [int]   $HealthTimeoutSec = 240
)

$ErrorActionPreference = 'Stop'
$BaseUrl = "http://127.0.0.1:$Port"
$single  = ($Devices -notmatch ',')

# ---- clean any stray server on our port ------------------------------------
Get-Process llama-server -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# ---- env levers ------------------------------------------------------------
$env:GGML_VK_VISIBLE_DEVICES = $Devices
$env:GGML_VK_DISABLE_COOPMAT = $(if ($Coopmat) { '0' } else { '1' })

# ---- server args -----------------------------------------------------------
$serverArgs = @(
    '-m', $ModelPath,
    '-ngl', '99',
    '-fa', $Fa,
    '--no-mmap', '-dio',
    '-fit', 'off',
    '-c', "$Ctx",
    '-ctk', $KvType, '-ctv', $KvType,
    '--host', '127.0.0.1', '--port', "$Port"
)
if (-not $single) { $serverArgs += @('-sm', $Sm, '-ts', $Ts) } else { $serverArgs += @('-sm', 'none') }

$logDir = Join-Path $env:TEMP ("bench-" + $Label.Replace(' ','_') + "-" + (Get-Date -Format 'HHmmss'))
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stdoutLog = Join-Path $logDir 'server.stdout.log'
$stderrLog = Join-Path $logDir 'server.stderr.log'

Write-Output ("==== CONFIG: $Label ====")
Write-Output ("  devices=$Devices  split=" + $(if($single){'SINGLE (-sm none)'}else{"$Sm ts=$Ts"}) + "  coopmat=" + $(if($Coopmat){'ON'}else{'OFF'}) + "  kv=$KvType  ctx=$Ctx  n_predict=$NPredict")

$loadStart = Get-Date
$proc = Start-Process -FilePath $LlamaServerBin -ArgumentList $serverArgs `
            -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog `
            -PassThru -WindowStyle Hidden

try {
    # ---- wait for /health ---------------------------------------------------
    $deadline = (Get-Date).AddSeconds($HealthTimeoutSec)
    $healthy = $false
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) { throw "llama-server exited during load (code $($proc.ExitCode)); see $stderrLog" }
        try {
            $r = Invoke-WebRequest -Uri ($BaseUrl + '/health') -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
            if ($r.StatusCode -eq 200) { $healthy = $true; break }
        } catch {}
        Start-Sleep -Seconds 2
    }
    if (-not $healthy) { throw "not healthy in $HealthTimeoutSec s; see $stderrLog" }
    $loadSec = [math]::Round(((Get-Date) - $loadStart).TotalSeconds, 1)

    # ---- did coopmat actually engage? (from startup log) --------------------
    $coopLine = (Select-String -Path $stderrLog -Pattern 'coopmat|matrix cores|matrix core' -ErrorAction SilentlyContinue |
                 Select-Object -First 2 | ForEach-Object { $_.Line.Trim() }) -join ' | '

    # ---- the measured prompt ------------------------------------------------
    if ($Tiny) { $prompt = "Write a detailed essay about the history of computing." }
    else       { $prompt = Get-Content -LiteralPath $ContextFile -Raw -Encoding UTF8 }
    # PS 5.1 ConvertTo-Json serializes large/provider-sourced strings as
    # {"value":...,"Count":...} objects (known bug) -- llama-server rejects that.
    # JavaScriptSerializer emits the string as a plain JSON string.
    Add-Type -AssemblyName System.Web.Extensions
    $ser = New-Object System.Web.Script.Serialization.JavaScriptSerializer
    $ser.MaxJsonLength = [int]::MaxValue
    $body = $ser.Serialize(@{ prompt = [string]$prompt; n_predict = $NPredict; stream = $false; cache_prompt = $false; temperature = 0.7; top_p = 0.95 })
    $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    $resp = Invoke-RestMethod -Uri ($BaseUrl + '/completion') -Method Post -Body $bodyBytes `
                              -ContentType 'application/json; charset=utf-8' -TimeoutSec 600

    $t = $resp.timings
    $prefillTps = [math]::Round($t.prompt_per_second, 1)
    $decodeTps  = [math]::Round($t.predicted_per_second, 2)

    Write-Output ("  load: ${loadSec}s   prefill: $($t.prompt_n) tok @ $prefillTps t/s   DECODE: $($t.predicted_n) tok @ $decodeTps t/s")
    if ($coopLine) { Write-Output ("  [coopmat probe] $coopLine") }
    Write-Output ("RESULT`t$Label`tload=${loadSec}s`tprefill=$prefillTps`tdecode=$decodeTps`tcoopmat=" + $(if($Coopmat){'ON'}else{'OFF'}) + "`tdevices=$Devices`tkv=$KvType")
}
finally {
    if ($proc -and -not $proc.HasExited) {
        try { $proc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    Start-Sleep -Seconds 2
}
