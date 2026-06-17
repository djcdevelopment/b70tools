param(
    [Parameter(Mandatory=$true)] [string]$RunDir,
    [ValidateSet('Server', 'Cli')] [string]$Mode = 'Server',
    [string]$Label = 'manual',
    [string]$PromptFile = '',
    [string]$PromptText = '',
    [string]$BaseUrl = 'http://127.0.0.1:8080',
    [int]$MaxTokens = 256,
    [double]$Temperature = 0.2,
    [string]$LlamaCliBin = 'D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe',
    [string]$ModelPath = '',
    [string]$VisibleDevices = '1',
    [string]$BindingIdentity = '',
    [bool]$DisableCoopmat = $true,
    [string[]]$ExtraCliArgs = @()
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NoBom.ps1')

if (-not (Test-Path $RunDir)) { throw "run directory not found: $RunDir" }

if ($PromptFile) {
    if (-not (Test-Path $PromptFile)) { throw "prompt file not found: $PromptFile" }
    $prompt = Get-Content -LiteralPath $PromptFile -Raw -Encoding UTF8
} elseif ($PromptText) {
    $prompt = $PromptText
} else {
    throw 'Provide -PromptFile or -PromptText.'
}

$safeLabel = ($Label.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
if (-not $safeLabel) { $safeLabel = 'stimulus' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outBase = Join-Path $RunDir ("inference-$stamp-$safeLabel")
$responsePath = "$outBase.response.txt"
$rawPath = "$outBase.raw.json"
$requestsPath = Join-Path $RunDir 'inference-requests.jsonl'

function Write-RequestRecord {
    param([hashtable]$Record)
    Append-JsonlNoBom -Path $requestsPath -Value $Record -Depth 10
}

function Invoke-LlamaServerCompletion {
    param(
        [string]$Url,
        [string]$Text,
        [int]$NPredict,
        [double]$Temp
    )
    $body = @{
        prompt = $Text
        n_predict = $NPredict
        stream = $false
        cache_prompt = $true
        temperature = $Temp
    } | ConvertTo-Json -Depth 8 -Compress
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($body)
    Invoke-RestMethod -Uri ($Url.TrimEnd('/') + '/completion') -Method Post `
        -Body $bytes `
        -ContentType 'application/json; charset=utf-8' `
        -TimeoutSec 600
}

$started = Get-Date
$record = @{
    label = $Label
    mode = $Mode
    started_at = $started.ToString('o')
    prompt_bytes = [System.Text.Encoding]::UTF8.GetByteCount($prompt)
    max_tokens = $MaxTokens
    binding_identity = $BindingIdentity
    disable_coopmat = $DisableCoopmat
    response_path = $responsePath
    raw_path = $rawPath
}

try {
    if ($Mode -eq 'Server') {
        $record.base_url = $BaseUrl
        $resp = Invoke-LlamaServerCompletion -Url $BaseUrl -Text $prompt -NPredict $MaxTokens -Temp $Temperature
        Write-JsonNoBom -Path $rawPath -Value $resp -Depth 20
        $content = ''
        if ($resp.content) { $content = [string]$resp.content }
        Write-TextNoBom -Path $responsePath -Content $content
        $record.tokens_predicted = $resp.tokens_predicted
        $record.tokens_evaluated = $resp.tokens_evaluated
        $record.timings = $resp.timings
        $record.stop = $resp.stop
    } else {
        if (-not (Test-Path $LlamaCliBin)) { throw "llama-cli not found: $LlamaCliBin" }
        if (-not $ModelPath) { throw 'Cli mode requires -ModelPath.' }
        if (-not (Test-Path $ModelPath)) { throw "model not found: $ModelPath" }

        $oldVisible = $env:GGML_VK_VISIBLE_DEVICES
        $oldCoop = $env:GGML_VK_DISABLE_COOPMAT
        $env:GGML_VK_VISIBLE_DEVICES = $VisibleDevices
        if ($DisableCoopmat) { $env:GGML_VK_DISABLE_COOPMAT = '1' }
        try {
            $args = @('-m', $ModelPath, '-ngl', '99', '--no-mmap', '-dio', '-n', "$MaxTokens")
            if ($PromptFile) {
                $args += @('-f', $PromptFile)
            } else {
                $args += @('-p', $prompt)
            }
            $args += $ExtraCliArgs
            $record.llama_cli_bin = $LlamaCliBin
            $record.model_path = $ModelPath
            $record.extra_cli_args = $ExtraCliArgs
            $output = & $LlamaCliBin @args 2>&1 | Out-String
            Write-TextNoBom -Path $responsePath -Content $output
            $record.exit_code = $LASTEXITCODE
            if ($LASTEXITCODE -ne 0) { throw "llama-cli exited $LASTEXITCODE" }
        }
        finally {
            $env:GGML_VK_VISIBLE_DEVICES = $oldVisible
            $env:GGML_VK_DISABLE_COOPMAT = $oldCoop
        }
    }

    $finished = Get-Date
    $record.finished_at = $finished.ToString('o')
    $record.wall_ms = [int]($finished - $started).TotalMilliseconds
    $record.status = 'ok'
    Write-RequestRecord -Record $record
    Write-Output "[ok] $Label wall_ms=$($record.wall_ms) response=$responsePath"
}
catch {
    $finished = Get-Date
    $record.finished_at = $finished.ToString('o')
    $record.wall_ms = [int]($finished - $started).TotalMilliseconds
    $record.status = 'error'
    $record.error = $_.Exception.Message
    Write-RequestRecord -Record $record
    throw
}
