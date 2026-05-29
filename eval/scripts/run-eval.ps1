# run-eval.ps1
# Stateless multi-round eval driver. Architecture (Rev 2):
#
#   1. Pre-flight host RAM safety check (Win32 — refuses to even start b70tools
#      if host is already pinned).
#   2. Start b70tools telemetry collector in background. Wait baseline.
#   3. Start llama-server with the operator-validated dual-B70 layer-split
#      recipe (see D:\work\battlemage\arc-b70-dual-70b-windows-vulkan.md).
#      Wait for /health.
#   4. Issue a small WARMUP /completion call to force VRAM commit on both cards.
#      Wait for telemetry to settle.
#   5. Call `b70tools verdict --json` against the telemetry stream.
#      If status != "healthy", STOP llama-server + b70tools, exit 2.
#      This is the BIOS-flash safety gate.
#   6. For each (round, prompt) — 15 calls total — POST to /completion with
#      the full injected context (snapshot + prior responses + round preface
#      + prompt task). Save response + per-call timings to disk.
#   7. After all calls, take a FINAL verdict snapshot.
#   8. Stop llama-server, stop b70tools, write manifest.
#
# Usage:
#   .\run-eval.ps1 -ConfigName <name> -ModelPath <path>
#                  [-SnapshotPath <path>] [-Ctx 32768] [-MaxTokens 1500]
#                  [-Port 8080] [-Host 127.0.0.1]
#
# Example (Qwen 2.5 32B Q6_K dual layer-split):
#   .\run-eval.ps1 -ConfigName qwen25-32b-q6-dual `
#                  -ModelPath 'D:\work\battlemage\models\qwen2.5-32b-instruct-q6_K.gguf'

param(
    [Parameter(Mandatory=$true)] [string]$ConfigName,
    [Parameter(Mandatory=$true)] [string]$ModelPath,
    [string]$SnapshotPath  = '',
    [int]   $Ctx           = 32768,
    [int]   $MaxTokens     = 1500,
    [string]$KvType        = 'q8_0',   # 'f16' for max precision; 'q8_0' halves KV memory
    [int]   $Port          = 8080,
    [string]$ServerHost    = '127.0.0.1',
    [string]$LlamaServerBin = 'D:\work\battlemage\llamacpp-win-vulkan\llama-server.exe',
    [string]$B70Tools       = 'D:\work\b70tools\build\b70tools.exe',
    [double]$MaxHostUsedGbPreflight = 22.0,
    [int]   $HealthTimeoutSec       = 180,
    [int]   $WarmupTokens           = 64,
    [int]   $TelemetrySettleSec     = 10,
    [int]   $CompletionTimeoutSec   = 600,
    [int]   $RoundCount             = 3,
    [int]   $PromptCount            = 5
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$EvalRoot   = Split-Path -Parent $ScriptRoot

# ---------- helpers ----------------------------------------------------------

function Get-HostUsedGb {
    $os = Get-CimInstance Win32_OperatingSystem
    $totalGb = [math]::Round($os.TotalVisibleMemorySize / 1MB, 2)
    $freeGb  = [math]::Round($os.FreePhysicalMemory     / 1MB, 2)
    return [math]::Round($totalGb - $freeGb, 2)
}

function Wait-LlamaServerHealthy {
    param(
        [Parameter(Mandatory)] [string] $BaseUrl,
        [int]    $TimeoutSec = 180
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $r = Invoke-WebRequest -Uri ($BaseUrl + '/health') -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
            if ($r.StatusCode -eq 200) {
                $j = $null
                try { $j = $r.Content | ConvertFrom-Json } catch {}
                if (-not $j -or ($j.status -in @('ok','loading model','loaded','ready') -or $r.StatusCode -eq 200)) {
                    return $true
                }
            }
        } catch {
            # 503 (loading) or connection refused while binding: keep waiting
        }
        Start-Sleep -Seconds 2
    }
    return $false
}

function Invoke-LlamaCompletion {
    param(
        [Parameter(Mandatory)] [string] $BaseUrl,
        [Parameter(Mandatory)] [string] $PromptText,
        [int]    $NPredict       = 64,
        [bool]   $CachePrompt    = $false,
        [int]    $TimeoutSec     = 600
    )
    $body = @{
        prompt       = $PromptText
        n_predict    = $NPredict
        stream       = $false
        cache_prompt = $CachePrompt
        temperature  = 0.7
        top_p        = 0.95
    } | ConvertTo-Json -Depth 6 -Compress
    # PS5.1's Invoke-RestMethod sends string bodies in cp1252 by default,
    # which mangles any non-ASCII into ill-formed UTF-8 over the wire. Force
    # UTF-8 by passing the body as bytes with an explicit charset header.
    $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    return Invoke-RestMethod -Uri ($BaseUrl + '/completion') -Method Post `
                              -Body $bodyBytes `
                              -ContentType 'application/json; charset=utf-8' `
                              -TimeoutSec $TimeoutSec
}

function Invoke-Verdict {
    param([Parameter(Mandatory)] [string] $TelemetryDir)
    $raw = & $B70Tools verdict $TelemetryDir --json 2>&1 | Out-String
    $code = $LASTEXITCODE
    $json = $null
    try { $json = $raw.Trim() | ConvertFrom-Json } catch {}
    return [pscustomobject]@{
        exit_code = $code
        raw       = $raw
        verdict   = $json
    }
}

# ---------- resolve inputs ---------------------------------------------------

if (-not $SnapshotPath) {
    $latest = Get-ChildItem -Path (Join-Path $EvalRoot 'snapshots') -Filter 'snapshot-*.md' -ErrorAction SilentlyContinue |
              Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No snapshot found; run build-snapshot.ps1 first.' }
    $SnapshotPath = $latest.FullName
}
foreach ($p in @($SnapshotPath, $ModelPath, $LlamaServerBin, $B70Tools)) {
    if (-not (Test-Path $p)) { throw "Required file not found: $p" }
}
. (Join-Path $ScriptRoot 'prompts.ps1')

# ---------- output layout ----------------------------------------------------

$RunDir = Join-Path $EvalRoot ('runs/' + $ConfigName)
if (Test-Path $RunDir) {
    $stamp  = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $RunDir = Join-Path $EvalRoot ('runs/' + $ConfigName + '-' + $stamp)
}
New-Item -ItemType Directory -Force -Path $RunDir | Out-Null
Copy-Item -LiteralPath $SnapshotPath -Destination (Join-Path $RunDir 'snapshot.md') -Force
$TelemetryDir = Join-Path $RunDir 'telemetry'
$ServerLogDir = Join-Path $RunDir 'server'
$VerdictDir   = Join-Path $RunDir 'verdicts'
New-Item -ItemType Directory -Force -Path $TelemetryDir | Out-Null
New-Item -ItemType Directory -Force -Path $ServerLogDir | Out-Null
New-Item -ItemType Directory -Force -Path $VerdictDir   | Out-Null

$BaseUrl = "http://${ServerHost}:${Port}"

$manifest = [ordered]@{
    config_name        = $ConfigName
    model_path         = $ModelPath
    snapshot_path      = $SnapshotPath
    snapshot_bytes     = (Get-Item $SnapshotPath).Length
    ctx                = $Ctx
    max_tokens         = $MaxTokens
    base_url           = $BaseUrl
    llama_server_bin   = $LlamaServerBin
    b70tools_bin       = $B70Tools
    flags = @{
        ngl = 99; sm = 'layer'; ts = '1,1'; fa = 'on'; fit = 'off'; mmap = $false; dio = $true
        ctk = $KvType; ctv = $KvType
        env = @{ GGML_VK_VISIBLE_DEVICES = '0,1'; GGML_VK_DISABLE_COOPMAT = '1' }
    }
    started_at         = (Get-Date).ToString('o')
    rounds             = @{}
}

# ---------- pre-flight: host RAM safety floor --------------------------------

$preflightUsed = Get-HostUsedGb
Write-Output ("[preflight] host RAM used = $preflightUsed GB (threshold $MaxHostUsedGbPreflight GB)")
if ($preflightUsed -gt $MaxHostUsedGbPreflight) {
    throw "preflight: host RAM used $preflightUsed GB exceeds $MaxHostUsedGbPreflight GB -- close apps or reboot before running eval. Protects against the OOM cascade that has caused non-POST / BIOS reflash on this rig."
}

# ---------- start b70tools telemetry -----------------------------------------

$telemetryProc = Start-Process -FilePath $B70Tools `
    -ArgumentList @('run', '--ticks', '0', '--out', $TelemetryDir) `
    -PassThru -WindowStyle Hidden
$manifest.b70tools_pid = $telemetryProc.Id
Write-Output ("[telemetry] b70tools PID = $($telemetryProc.Id) out=" + $TelemetryDir)
Start-Sleep -Seconds 12  # baseline window before any workload

# ---------- start llama-server -----------------------------------------------

$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'

$serverArgs = @(
    '-m', $ModelPath,
    '-ngl', '99',
    '-sm', 'layer', '-ts', '1,1',
    '-fa', 'on',
    '--no-mmap', '-dio',
    '-fit', 'off',
    '-c', "$Ctx",
    '-ctk', $KvType, '-ctv', $KvType,
    '--host', $ServerHost, '--port', "$Port"
)
# Qwen2.5's native training context is 32768. This llama-server build caps the
# slot context to n_ctx_train regardless of --rope-scaling flags, so we use the
# native 32K rather than fight the cap. Snapshots are sized to fit (~22K tokens
# leaves ~10K headroom for generation).

$serverStdout = Join-Path $ServerLogDir 'server.stdout.log'
$serverStderr = Join-Path $ServerLogDir 'server.stderr.log'
Write-Output ('[server] starting llama-server: ' + ($serverArgs -join ' '))
$serverProc = Start-Process -FilePath $LlamaServerBin -ArgumentList $serverArgs `
                            -RedirectStandardOutput $serverStdout `
                            -RedirectStandardError  $serverStderr `
                            -PassThru -WindowStyle Hidden
$manifest.llama_server_pid = $serverProc.Id

try {
    # ------ wait for /health ---------------------------------------------------
    Write-Output ("[server] waiting up to $HealthTimeoutSec s for /health ...")
    if (-not (Wait-LlamaServerHealthy -BaseUrl $BaseUrl -TimeoutSec $HealthTimeoutSec)) {
        throw "llama-server did not become healthy within $HealthTimeoutSec s; see $serverStderr"
    }
    Write-Output '[server] /health: OK'

    # ------ warmup prompt to force VRAM commit on both cards -------------------
    Write-Output ("[warmup] issuing warmup prompt ($WarmupTokens tokens) ...")
    $warmStart = Get-Date
    $warm = Invoke-LlamaCompletion -BaseUrl $BaseUrl -PromptText 'Hello.' `
                                   -NPredict $WarmupTokens -TimeoutSec 120
    $warmWallMs = [int]((Get-Date) - $warmStart).TotalMilliseconds
    $manifest.warmup = [ordered]@{
        wall_ms          = $warmWallMs
        tokens_predicted = $warm.tokens_predicted
        tokens_evaluated = $warm.tokens_evaluated
        timings          = $warm.timings
    }
    Write-Output ("[warmup] " + $warm.tokens_predicted + " tokens in " + $warmWallMs + " ms")

    Start-Sleep -Seconds $TelemetrySettleSec

    # ------ pre-eval verdict gate ---------------------------------------------
    Write-Output '[gate] requesting verdict from b70tools ...'
    $preGate = Invoke-Verdict -TelemetryDir $TelemetryDir
    $preGate.raw | Out-File -FilePath (Join-Path $VerdictDir 'pre-eval.json') -Encoding utf8 -Force
    Write-Output ('[gate] exit=' + $preGate.exit_code)
    if ($preGate.verdict) {
        Write-Output ('[gate] status=' + $preGate.verdict.status)
        foreach ($i in $preGate.verdict.issues) { Write-Output ('[gate]   - ' + $i) }
    }
    $manifest.pre_eval_verdict = $preGate.verdict
    if ($preGate.exit_code -ne 0) {
        throw ('preflight gate failed (exit=' + $preGate.exit_code + ') — refusing to run eval; see verdicts/pre-eval.json')
    }

    # ------ run rounds --------------------------------------------------------
    $Separator = "`n`n---`n`n"
    $Snapshot  = Get-Content -LiteralPath $SnapshotPath -Raw -Encoding UTF8

    for ($round = 1; $round -le $RoundCount; $round++) {
        $RoundDir = Join-Path $RunDir ('round-' + $round)
        New-Item -ItemType Directory -Force -Path $RoundDir | Out-Null
        $manifest.rounds[$round.ToString()] = [ordered]@{ prompts = @{}; wall_ms = 0 }
        $roundStart = Get-Date

        $promptsThisRound = $PromptSequence | Select-Object -First $PromptCount
        foreach ($p in $promptsThisRound) {
            $pno         = $p.id
            $promptOut   = Join-Path $RoundDir ('prompt-' + $pno + '.md')
            $injectedOut = Join-Path $RoundDir ('prompt-' + $pno + '-injected-context.md')

            # build context
            $sb = New-Object System.Text.StringBuilder
            $null = $sb.AppendLine('# REPOSITORY SNAPSHOT'); $null = $sb.AppendLine(''); $null = $sb.Append($Snapshot); $null = $sb.AppendLine($Separator)
            if ($round -ge 2) {
                $priorPath = Join-Path (Join-Path $RunDir 'round-1') ('prompt-' + $pno + '.md')
                if (Test-Path $priorPath) {
                    $null = $sb.AppendLine('# YOUR PRIOR RESPONSE (Round 1, Prompt ' + $pno + ')'); $null = $sb.AppendLine('')
                    $null = $sb.Append((Get-Content -LiteralPath $priorPath -Raw -Encoding UTF8)); $null = $sb.AppendLine($Separator)
                }
            }
            if ($round -ge 3) {
                $priorPath = Join-Path (Join-Path $RunDir 'round-2') ('prompt-' + $pno + '.md')
                if (Test-Path $priorPath) {
                    $null = $sb.AppendLine('# YOUR PRIOR RESPONSE (Round 2, Prompt ' + $pno + ')'); $null = $sb.AppendLine('')
                    $null = $sb.Append((Get-Content -LiteralPath $priorPath -Raw -Encoding UTF8)); $null = $sb.AppendLine($Separator)
                }
            }
            $preface = $RoundPreface[$round]
            if ($preface) {
                $null = $sb.AppendLine('# ROUND ' + $round + ' INSTRUCTION'); $null = $sb.AppendLine('')
                $null = $sb.Append($preface.Trim()); $null = $sb.AppendLine($Separator)
            }
            $null = $sb.AppendLine('# YOUR TASK'); $null = $sb.AppendLine('')
            $null = $sb.AppendLine('Title: ' + $p.title); $null = $sb.AppendLine('')
            $null = $sb.Append($p.text.Trim()); $null = $sb.AppendLine(''); $null = $sb.AppendLine('')
            $null = $sb.AppendLine('Begin your response now.')

            $full = $sb.ToString()
            $full | Out-File -FilePath $injectedOut -Encoding UTF8 -Force

            Write-Output ('  [r' + $round + ' / p' + $pno + '] ' + $p.title)
            $invStart = Get-Date
            try {
                $resp = Invoke-LlamaCompletion -BaseUrl $BaseUrl -PromptText $full `
                                               -NPredict $MaxTokens -TimeoutSec $CompletionTimeoutSec
                $invEnd = Get-Date
                $wallMs = [int]($invEnd - $invStart).TotalMilliseconds

                $content = ''
                if ($resp.content) { $content = [string]$resp.content }
                $content = $content.TrimStart("`n","`r"," ").TrimEnd()
                $content | Out-File -FilePath $promptOut -Encoding UTF8 -Force

                $manifest.rounds[$round.ToString()].prompts[$pno.ToString()] = [ordered]@{
                    title              = $p.title
                    wall_ms            = $wallMs
                    tokens_predicted   = $resp.tokens_predicted
                    tokens_evaluated   = $resp.tokens_evaluated
                    timings            = $resp.timings
                    output_bytes       = (Get-Item $promptOut).Length
                    injected_bytes     = (Get-Item $injectedOut).Length
                    stop               = $resp.stop
                    stopped_eos        = $resp.stopped_eos
                    stopped_limit      = $resp.stopped_limit
                    truncated          = $resp.truncated
                }
            } catch {
                $invEnd = Get-Date
                $wallMs = [int]($invEnd - $invStart).TotalMilliseconds
                $errMsg = $_.Exception.Message
                "[ERROR] /completion failed after $wallMs ms: $errMsg" | Out-File -FilePath $promptOut -Encoding UTF8 -Force
                $manifest.rounds[$round.ToString()].prompts[$pno.ToString()] = [ordered]@{
                    title = $p.title; wall_ms = $wallMs; error = $errMsg
                }
            }
        }

        $manifest.rounds[$round.ToString()].wall_ms = [int]((Get-Date) - $roundStart).TotalMilliseconds
        Write-Output ('  round ' + $round + ' wall: ' + $manifest.rounds[$round.ToString()].wall_ms + ' ms')

        # mid-eval verdict snapshot
        $midGate = Invoke-Verdict -TelemetryDir $TelemetryDir
        $midGate.raw | Out-File -FilePath (Join-Path $VerdictDir ('round-' + $round + '.json')) -Encoding utf8 -Force
        if ($midGate.verdict) {
            Write-Output ('[gate] mid-round verdict status=' + $midGate.verdict.status)
        }
    }

    # ------ final verdict snapshot --------------------------------------------
    $postGate = Invoke-Verdict -TelemetryDir $TelemetryDir
    $postGate.raw | Out-File -FilePath (Join-Path $VerdictDir 'post-eval.json') -Encoding utf8 -Force
    $manifest.post_eval_verdict = $postGate.verdict
}
finally {
    # ------ shutdown ----------------------------------------------------------
    if ($serverProc -and -not $serverProc.HasExited) {
        Write-Output '[server] stopping llama-server ...'
        try { $serverProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
        try { $serverProc | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue } catch {}
    }
    if ($telemetryProc -and -not $telemetryProc.HasExited) {
        Write-Output '[telemetry] stopping b70tools ...'
        try { $telemetryProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }
    Start-Sleep -Seconds 2
    $manifest.finished_at = (Get-Date).ToString('o')
    $manifest | ConvertTo-Json -Depth 12 | Out-File -FilePath (Join-Path $RunDir 'manifest.json') -Encoding UTF8 -Force

    Write-Output ''
    Write-Output ('Eval complete: ' + $RunDir)
    Write-Output ('  Telemetry: ' + (Join-Path $TelemetryDir 'events.jsonl'))
    Write-Output ('  Verdicts:  ' + $VerdictDir)
    Write-Output ('  Manifest:  ' + (Join-Path $RunDir 'manifest.json'))
}
