# run-baselines-stateful.ps1 — sequential stateful eval launcher.
# Same models as run-baselines.ps1 but invokes run-eval-stateful.ps1.

param(
    [int]   $RoundCount      = 3,
    [int]   $PromptCount     = 5,
    [int]   $MaxTokens       = 1500,
    [int]   $Ctx             = 32768,
    [string]$KvType          = 'q8_0',
    [string[]] $Models       = @(
        'D:\work\battlemage\models\qwen2.5-coder-32b-instruct-q5_k_m.gguf',
        'D:\work\battlemage\models\qwen2.5-32b-instruct-q6_K.gguf'
    )
)

$ErrorActionPreference = 'Continue'
$Stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
$RunRoot = "D:\work\b70tools\eval\runs\stateful-baselines-$Stamp"
New-Item -ItemType Directory -Force -Path $RunRoot | Out-Null
$LogPath = Join-Path $RunRoot 'launcher.log'

function Write-Log {
    param([string]$msg)
    $line = ('[' + (Get-Date).ToString('o') + '] ' + $msg)
    Add-Content -Path $LogPath -Value $line -Encoding UTF8
    Write-Output $line
}

Write-Log ('starting STATEFUL baselines: ' + ($Models -join ', '))
Write-Log ('RoundCount=' + $RoundCount + ', PromptCount=' + $PromptCount + ', MaxTokens=' + $MaxTokens + ', Ctx=' + $Ctx + ', KvType=' + $KvType)

$results = @()

foreach ($modelPath in $Models) {
    if (-not (Test-Path $modelPath)) {
        Write-Log ('SKIP missing model: ' + $modelPath)
        $results += [pscustomobject]@{ model = $modelPath; status = 'skip-missing'; exit_code = -1 }
        continue
    }
    $basename = [System.IO.Path]::GetFileNameWithoutExtension($modelPath)
    $configName = 'stateful-' + $basename + '-' + $Stamp

    Write-Log ('==== STARTING ' + $configName + ' ====')
    $startedAt = Get-Date

    Get-Process llama-server, llama-cli, llama-completion, b70tools -ErrorAction SilentlyContinue |
        Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 5

    & 'D:\work\b70tools\eval\scripts\run-eval-stateful.ps1' `
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
        model = $modelPath; config_name = $configName
        status = if ($exitCode -eq 0) { 'ok' } else { 'failed' }
        exit_code = $exitCode; duration_min = $durationMin
    }
}

Write-Log 'all stateful baselines complete'
$summary = $results | ConvertTo-Json -Depth 4
$summary | Out-File -FilePath (Join-Path $RunRoot 'summary.json') -Encoding UTF8 -Force
Write-Log ('summary -> ' + (Join-Path $RunRoot 'summary.json'))

Write-Output ''
Write-Output '===== STATEFUL BASELINE RUN SUMMARY ====='
foreach ($r in $results) {
    Write-Output ('  ' + $r.config_name + '  status=' + $r.status + '  exit=' + $r.exit_code + '  duration=' + $r.duration_min + ' min')
}
