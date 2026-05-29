# run-eval-stateful.ps1 — STATEFUL counterpart of run-eval.ps1.
#
# Where run-eval.ps1 is stateless (each /completion call rebuilds the full
# context including prior responses as injected text), this script holds a
# real chat conversation per prompt. The server retains KV across calls.
#
# Per prompt (5 of them), within a single conversation thread:
#   Round 1 — user sends: system + (snapshot + task)
#             assistant responds. We save round-1/prompt-N.md.
#   Round 2 — user sends just a "review your prior" preface.
#             The model sees its prior response in chat history.
#   Round 3 — user sends a "stabilization" preface.
#             The model sees both prior responses in chat history.
#
# Conversations are independent across prompts (we don't bleed prompt 1
# context into prompt 2). Within a prompt's thread, cache_prompt=true means
# the server reuses the round-1 KV prefix on rounds 2 + 3.
#
# Output layout matches run-eval.ps1 (round-N/prompt-M.md) so stateless and
# stateful runs are directly comparable.

param(
    [Parameter(Mandatory=$true)] [string]$ConfigName,
    [Parameter(Mandatory=$true)] [string]$ModelPath,
    [string]$SnapshotPath  = '',
    [int]   $Ctx           = 32768,
    [int]   $MaxTokens     = 1500,
    [string]$KvType        = 'q8_0',
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
    param([Parameter(Mandatory)] [string] $BaseUrl, [int] $TimeoutSec = 180)
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $r = Invoke-WebRequest -Uri ($BaseUrl + '/health') -TimeoutSec 5 -UseBasicParsing -ErrorAction Stop
            if ($r.StatusCode -eq 200) { return $true }
        } catch {}
        Start-Sleep -Seconds 2
    }
    return $false
}

function Invoke-LlamaChat {
    param(
        [Parameter(Mandatory)] [string] $BaseUrl,
        [Parameter(Mandatory)] [array]  $Messages,
        [int]    $MaxTokens      = 1500,
        [bool]   $CachePrompt    = $true,
        [int]    $TimeoutSec     = 600
    )
    $body = @{
        model        = 'b70'
        messages     = $Messages
        max_tokens   = $MaxTokens
        stream       = $false
        cache_prompt = $CachePrompt
        temperature  = 0.7
        top_p        = 0.95
    } | ConvertTo-Json -Depth 8 -Compress
    $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    return Invoke-RestMethod -Uri ($BaseUrl + '/v1/chat/completions') -Method Post `
                              -Body $bodyBytes `
                              -ContentType 'application/json; charset=utf-8' `
                              -TimeoutSec $TimeoutSec
}

function Invoke-LlamaCompletion {
    # Used only for warmup. Stateless one-shot.
    param(
        [Parameter(Mandatory)] [string] $BaseUrl,
        [Parameter(Mandatory)] [string] $PromptText,
        [int] $NPredict = 64, [int] $TimeoutSec = 120
    )
    $body = @{ prompt = $PromptText; n_predict = $NPredict; stream = $false; cache_prompt = $false } |
            ConvertTo-Json -Depth 4 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    return Invoke-RestMethod -Uri ($BaseUrl + '/completion') -Method Post `
                              -Body $bytes -ContentType 'application/json; charset=utf-8' `
                              -TimeoutSec $TimeoutSec
}

function Invoke-Verdict {
    param([Parameter(Mandatory)] [string] $TelemetryDir)
    $raw = & $B70Tools verdict $TelemetryDir --json 2>&1 | Out-String
    $code = $LASTEXITCODE
    $json = $null
    try { $json = $raw.Trim() | ConvertFrom-Json } catch {}
    return [pscustomobject]@{ exit_code = $code; raw = $raw; verdict = $json }
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
$ConvoDir     = Join-Path $RunDir 'conversations'
foreach ($d in @($TelemetryDir, $ServerLogDir, $VerdictDir, $ConvoDir)) {
    New-Item -ItemType Directory -Force -Path $d | Out-Null
}

$BaseUrl = "http://${ServerHost}:${Port}"

# Stateful-mode round prefaces: brief, since prior assistant turns are already
# in the conversation history. The model doesn't need "for your reference" text.
$StatefulPreface = @{
    2 = @"
Now review your prior analysis above. Where your prior reasoning remains correct, keep it. Where it was wrong, vague, or hallucinated, correct it explicitly and note what changed. Where new perspective contradicts your prior claims, acknowledge the contradiction directly. Your goal is improvement, not repetition.

If your prior response was substantially correct, a shorter response that surgically updates the analysis is preferred over a full rewrite.
"@
    3 = @"
This is the stabilization round. Produce your final analysis. Reconcile any contradictions across rounds. Mark any claim where your confidence has changed across rounds. Persistent contradictions across all three rounds will count against the model's score; explicit acknowledgement and correction will count in its favor.

Aim for a final response that a technical lead could act on without further clarification.
"@
}

$SystemMessage = "You are an autonomous technical analyst examining an unfamiliar software repository. Provide your analysis directly. The repository snapshot is provided in the first user message."

$manifest = [ordered]@{
    config_name        = $ConfigName
    mode               = 'stateful'
    model_path         = $ModelPath
    snapshot_path      = $SnapshotPath
    snapshot_bytes     = (Get-Item $SnapshotPath).Length
    ctx                = $Ctx
    max_tokens         = $MaxTokens
    base_url           = $BaseUrl
    flags = @{
        ngl = 99; sm = 'layer'; ts = '1,1'; fa = 'on'; fit = 'off'; mmap = $false; dio = $true
        ctk = $KvType; ctv = $KvType
        env = @{ GGML_VK_VISIBLE_DEVICES = '0,1'; GGML_VK_DISABLE_COOPMAT = '1' }
    }
    started_at         = (Get-Date).ToString('o')
    prompts            = @{}
}

# ---------- pre-flight: host RAM safety floor --------------------------------

$preflightUsed = Get-HostUsedGb
Write-Output ("[preflight] host RAM used = $preflightUsed GB (threshold $MaxHostUsedGbPreflight GB)")
if ($preflightUsed -gt $MaxHostUsedGbPreflight) {
    throw "preflight: host RAM used $preflightUsed GB exceeds $MaxHostUsedGbPreflight GB -- close apps or reboot before running eval."
}

# ---------- start b70tools telemetry -----------------------------------------

$telemetryProc = Start-Process -FilePath $B70Tools `
    -ArgumentList @('run', '--ticks', '0', '--out', $TelemetryDir) `
    -PassThru -WindowStyle Hidden
$manifest.b70tools_pid = $telemetryProc.Id
Write-Output ("[telemetry] b70tools PID = $($telemetryProc.Id) out=" + $TelemetryDir)
Start-Sleep -Seconds 12

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
$serverStdout = Join-Path $ServerLogDir 'server.stdout.log'
$serverStderr = Join-Path $ServerLogDir 'server.stderr.log'
Write-Output ('[server] starting llama-server: ' + ($serverArgs -join ' '))
$serverProc = Start-Process -FilePath $LlamaServerBin -ArgumentList $serverArgs `
                            -RedirectStandardOutput $serverStdout `
                            -RedirectStandardError  $serverStderr `
                            -PassThru -WindowStyle Hidden
$manifest.llama_server_pid = $serverProc.Id

try {
    Write-Output ("[server] waiting up to $HealthTimeoutSec s for /health ...")
    if (-not (Wait-LlamaServerHealthy -BaseUrl $BaseUrl -TimeoutSec $HealthTimeoutSec)) {
        throw "llama-server did not become healthy within $HealthTimeoutSec s; see $serverStderr"
    }
    Write-Output '[server] /health: OK'

    # warmup
    Write-Output ("[warmup] issuing warmup prompt ($WarmupTokens tokens) ...")
    $warm = Invoke-LlamaCompletion -BaseUrl $BaseUrl -PromptText 'Hello.' -NPredict $WarmupTokens
    Write-Output ('[warmup] ' + $warm.tokens_predicted + ' tokens')

    Start-Sleep -Seconds $TelemetrySettleSec

    # pre-eval verdict
    Write-Output '[gate] requesting verdict ...'
    $preGate = Invoke-Verdict -TelemetryDir $TelemetryDir
    $preGate.raw | Out-File -FilePath (Join-Path $VerdictDir 'pre-eval.json') -Encoding utf8 -Force
    Write-Output ('[gate] exit=' + $preGate.exit_code)
    if ($preGate.verdict) { Write-Output ('[gate] status=' + $preGate.verdict.status) }
    foreach ($i in $preGate.verdict.issues) { Write-Output ('[gate]   - ' + $i) }
    $manifest.pre_eval_verdict = $preGate.verdict
    if ($preGate.exit_code -ne 0) {
        throw ('preflight gate failed (exit=' + $preGate.exit_code + ')')
    }

    # ---------- run prompts as stateful conversations -------------------------
    $Snapshot = Get-Content -LiteralPath $SnapshotPath -Raw -Encoding UTF8
    $promptsToRun = $PromptSequence | Select-Object -First $PromptCount

    foreach ($p in $promptsToRun) {
        $pno = $p.id
        Write-Output ('  === PROMPT ' + $pno + ' (' + $p.title + ') ===')

        # Round-1 user message: snapshot + task
        $r1Sb = New-Object System.Text.StringBuilder
        $null = $r1Sb.AppendLine('# REPOSITORY SNAPSHOT')
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.Append($Snapshot)
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.AppendLine('# YOUR TASK')
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.AppendLine('Title: ' + $p.title)
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.Append($p.text.Trim())
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.AppendLine('')
        $null = $r1Sb.AppendLine('Begin your response now.')

        $messages = @(
            @{ role = 'system'; content = $SystemMessage }
            @{ role = 'user';   content = $r1Sb.ToString() }
        )

        $promptRecord = [ordered]@{ title = $p.title; rounds = @{} }

        for ($round = 1; $round -le $RoundCount; $round++) {
            $RoundDir = Join-Path $RunDir ('round-' + $round)
            New-Item -ItemType Directory -Force -Path $RoundDir | Out-Null
            $promptOut = Join-Path $RoundDir ('prompt-' + $pno + '.md')

            if ($round -ge 2) {
                $preface = $StatefulPreface[$round]
                $messages += @{ role = 'user'; content = $preface.Trim() }
            }

            Write-Output ('    [r' + $round + ' / p' + $pno + '] sending ' + $messages.Count + ' messages ...')
            $invStart = Get-Date
            try {
                $resp = Invoke-LlamaChat -BaseUrl $BaseUrl -Messages $messages `
                                         -MaxTokens $MaxTokens -CachePrompt $true `
                                         -TimeoutSec $CompletionTimeoutSec
                $invEnd = Get-Date
                $wallMs = [int]($invEnd - $invStart).TotalMilliseconds

                $content = ''
                if ($resp.choices -and $resp.choices[0].message -and $resp.choices[0].message.content) {
                    $content = [string]$resp.choices[0].message.content
                }
                $content = $content.TrimStart("`n","`r"," ").TrimEnd()
                $content | Out-File -FilePath $promptOut -Encoding UTF8 -Force

                # append the assistant turn to the conversation for next round
                $messages += @{ role = 'assistant'; content = $content }

                $promptRecord.rounds[$round.ToString()] = [ordered]@{
                    wall_ms          = $wallMs
                    usage            = $resp.usage
                    timings          = $resp.timings
                    finish_reason    = $resp.choices[0].finish_reason
                    output_bytes     = (Get-Item $promptOut).Length
                    messages_sent    = ($messages.Count - 1)  # -1 because we just appended the assistant
                }
            } catch {
                $invEnd = Get-Date
                $wallMs = [int]($invEnd - $invStart).TotalMilliseconds
                $errMsg = $_.Exception.Message
                "[ERROR] /v1/chat/completions failed after $wallMs ms: $errMsg" |
                    Out-File -FilePath $promptOut -Encoding UTF8 -Force
                $promptRecord.rounds[$round.ToString()] = [ordered]@{
                    wall_ms = $wallMs; error = $errMsg
                }
                # Don't continue with broken thread for this prompt
                break
            }
        }

        # save full conversation for audit
        $convoPath = Join-Path $ConvoDir ('prompt-' + $pno + '.json')
        $messages | ConvertTo-Json -Depth 8 | Out-File -FilePath $convoPath -Encoding UTF8 -Force

        $manifest.prompts[$pno.ToString()] = $promptRecord

        # inter-prompt verdict snapshot
        $midGate = Invoke-Verdict -TelemetryDir $TelemetryDir
        $midGate.raw | Out-File -FilePath (Join-Path $VerdictDir ('after-prompt-' + $pno + '.json')) -Encoding utf8 -Force
        if ($midGate.verdict) { Write-Output ('    [gate] status=' + $midGate.verdict.status) }
    }

    # final verdict snapshot
    $postGate = Invoke-Verdict -TelemetryDir $TelemetryDir
    $postGate.raw | Out-File -FilePath (Join-Path $VerdictDir 'post-eval.json') -Encoding utf8 -Force
    $manifest.post_eval_verdict = $postGate.verdict
}
finally {
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
    Write-Output ('Stateful eval complete: ' + $RunDir)
    Write-Output ('  Telemetry: ' + (Join-Path $TelemetryDir 'events.jsonl'))
    Write-Output ('  Verdicts:  ' + $VerdictDir)
    Write-Output ('  Convos:    ' + $ConvoDir)
    Write-Output ('  Manifest:  ' + (Join-Path $RunDir 'manifest.json'))
}
