# run-baselines.ps1 — sequential launcher for the two newest models in
# D:\work\battlemage\models\. Single rig, can't parallelize.
#
# Reads each model, calls run-eval.ps1 with the standard production knobs
# (3 rounds x 5 prompts x 1500 max tokens, -c 65536, KV q8_0), and logs the
# outcome of each. If one baseline aborts via gate trip, the script proceeds
# to the next model.

param(
    [int]   $RoundCount      = 3,
    [int]   $PromptCount     = 5,
    [int]   $MaxTokens       = 1500,
    [int]   $Ctx             = 65536,
    [string]$KvType          = 'q8_0',
    [string[]] $Models       = @(
        'D:\work\battlemage\models\qwen2.5-coder-32b-instruct-q5_k_m.gguf',
        'D:\work\battlemage\models\qwen2.5-32b-instruct-q6_K.gguf'
    )
)

$ErrorActionPreference = 'Continue'
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$Stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$RunRoot = "D:\work\b70tools\eval\runs\baselines-$Stamp"
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
$LogPath = Join-Path $RunRoot 'launcher.log'

function Write-Log {
    param([string]$msg)
    $line = ('[' + (Get-Date).ToString('o') + '] ' + $msg)
    Add-Content -Path $LogPath -Value $line -Encoding UTF8
    Write-Output $line
}

Write-Log ('starting baseline run: ' + ($Models -join ', '))
Write-Log ('RoundCount=' + $RoundCount + ', PromptCount=' + $PromptCount + ', MaxTokens=' + $MaxTokens + ', Ctx=' + $Ctx + ', KvType=' + $KvType)

$results = @()

foreach ($modelPath in $Models) {
    if (-not (Test-Path $modelPath)) {
        Write-Log ('SKIP missing model: ' + $modelPath)
        $results += [pscustomobject]@{ model = $modelPath; status = 'skip-missing'; exit_code = -1 }
        continue
    }

    # derive a stable config name from the model file basename
    $basename = [System.IO.Path]::GetFileNameWithoutExtension($modelPath)
    $configName = $basename + '-' + $Stamp

    Write-Log ('==== STARTING ' + $configName + ' ====')
    $startedAt = Get-Date

    # ensure clean state before each baseline
    Get-Process llama-server, llama-cli, llama-completion, b70tools -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 5

    & 'D:\work\b70tools\eval\scripts\run-eval.ps1' `
        -ConfigName $configName `
        -ModelPath $modelPath `
        -RoundCount $RoundCount -PromptCount $PromptCount `
        -MaxTokens $MaxTokens -Ctx $Ctx -KvType $KvType `
        2>&1 | Tee-Object -FilePath (Join-Path $RunRoot ($configName + '.console.log')) | Out-Null

    $exitCode = $LASTEXITCODE
    $finishedAt = Get-Date
    $durationMin = [math]::Round(($finishedAt - $startedAt).TotalMinutes, 2)

    Write-Log ('==== FINISHED ' + $configName + ' exit=' + $exitCode + ' duration=' + $durationMin + ' min ====')
    $results += [pscustomobject]@{
        model       = $modelPath
        config_name = $configName
        status      = if ($exitCode -eq 0) { 'ok' } else { 'failed' }
        exit_code   = $exitCode
        duration_min = $durationMin
    }
}

Write-Log 'all baselines complete'
$summary = $results | ConvertTo-Json -Depth 4
$summary | Out-File -FilePath (Join-Path $RunRoot 'summary.json') -Encoding UTF8 -Force
Write-Log ('summary written to ' + (Join-Path $RunRoot 'summary.json'))

# print readable summary
Write-Output ''
Write-Output '===== BASELINE RUN SUMMARY ====='
foreach ($r in $results) {
    Write-Output ('  ' + $r.config_name + '  status=' + $r.status + '  exit=' + $r.exit_code + '  duration=' + $r.duration_min + ' min')
}
