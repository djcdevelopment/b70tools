# run-gambit.ps1 — chains Qwen3-30B-A3B baseline (stateless) then stateful.
# Triggered manually after the current Q6_K stateful baseline finishes.
# Same eval protocol as the other runs: 3 rounds x 5 prompts x 1500 tokens.

param(
    [string]$GambitModel    = 'D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf',
    [int]   $RoundCount     = 3,
    [int]   $PromptCount    = 5,
    [int]   $MaxTokens      = 1500,
    [int]   $Ctx            = 32768,
    [string]$KvType         = 'q8_0'
)

if (-not (Test-Path $GambitModel)) {
    throw "Gambit model not found at: $GambitModel"
}

Write-Output ('===== GAMBIT (Qwen3-30B-A3B) BASELINE -> STATEFUL =====')
Write-Output ('Model: ' + $GambitModel)
Write-Output ('Settings: RoundCount=' + $RoundCount + ', PromptCount=' + $PromptCount + ', MaxTokens=' + $MaxTokens + ', Ctx=' + $Ctx + ', KvType=' + $KvType)
Write-Output ''

# Stage 1: stateless baseline
Write-Output '--- Stage 1: STATELESS baseline ---'
Get-Process llama-server, llama-cli, llama-completion, b70tools -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 5

& 'D:\work\b70tools\eval\scripts\run-baselines.ps1' `
    -Models @($GambitModel) `
    -RoundCount $RoundCount -PromptCount $PromptCount `
    -MaxTokens $MaxTokens -Ctx $Ctx -KvType $KvType
$statelessExit = $LASTEXITCODE
Write-Output ('Stage 1 exit=' + $statelessExit)

Start-Sleep -Seconds 10

# Stage 2: stateful baseline
Write-Output ''
Write-Output '--- Stage 2: STATEFUL baseline ---'
Get-Process llama-server, llama-cli, llama-completion, b70tools -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 5

& 'D:\work\b70tools\eval\scripts\run-baselines-stateful.ps1' `
    -Models @($GambitModel) `
    -RoundCount $RoundCount -PromptCount $PromptCount `
    -MaxTokens $MaxTokens -Ctx $Ctx -KvType $KvType
$statefulExit = $LASTEXITCODE
Write-Output ('Stage 2 exit=' + $statefulExit)

Write-Output ''
Write-Output ('===== GAMBIT RUNS COMPLETE: stateless=' + $statelessExit + ', stateful=' + $statefulExit + ' =====')
