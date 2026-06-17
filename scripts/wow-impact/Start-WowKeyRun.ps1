param(
    [int]$DurationSec = 3600,
    [int]$InferenceDelaySec = 420,
    [string]$RunRoot = 'D:\work\b70tools\runs',
    [string]$B70Tools = 'D:\work\b70tools\build\b70tools.exe',
    [string]$WowLogPath = 'D:\World of Warcraft\_retail_\Logs\WoWCombatLog-052926_040215.txt',
    [string]$LlamaCliBin = 'D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe',
    [string]$ModelPath = 'D:\work\battlemage\models\qwen2.5-14b-instruct-q4_K_M.gguf',
    [string]$VisibleDevices = '1',
    [string]$BindingIdentity = '',
    [int]$MaxTokens = 160
)

$ErrorActionPreference = 'Stop'

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptRoot)
. (Join-Path $scriptRoot 'NoBom.ps1')
$startRun = Join-Path $scriptRoot 'Start-WowImpactRun.ps1'
$stimulus = Join-Path $scriptRoot 'Invoke-InferenceStimulus.ps1'

if (-not (Test-Path $startRun)) { throw "missing Start-WowImpactRun.ps1: $startRun" }
if (-not (Test-Path $stimulus)) { throw "missing Invoke-InferenceStimulus.ps1: $stimulus" }
if (-not (Test-Path $WowLogPath)) { throw "WoW combat log not found: $WowLogPath" }
if (-not (Test-Path $B70Tools)) { throw "b70tools not found: $B70Tools" }

function Quote-Arg {
    param([string]$Value)
    return '"' + ($Value -replace '"', '\"') + '"'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runDir = Join-Path $RunRoot ("wow-impact-$stamp-key-live")
$outerLog = Join-Path $RunRoot ("wow-key-$stamp.orchestrator.log")
$promptFile = Join-Path $runDir 'stimulus-after-7m.prompt.md'
$stimulusLog = Join-Path $runDir 'stimulus-after-7m.stdout.log'
$stimulusErr = Join-Path $runDir 'stimulus-after-7m.stderr.log'

New-Item -ItemType Directory -Force -Path $runDir | Out-Null

Write-TextNoBom -Path $promptFile -Content @"
You are receiving a bounded, low-stakes live gameplay stimulus.

Task:
- Return exactly three terse bullets.
- First bullet: say this is a live benchmark stimulus.
- Second bullet: identify one thing a healer should watch in Mythic+.
- Third bullet: say whether the response completed.

Do not invent run details. Do not mention combat-log specifics.
"@

$launchInfo = [ordered]@{
    run_dir = $runDir
    started_at = (Get-Date).ToString('o')
    duration_sec = $DurationSec
    inference_delay_sec = $InferenceDelaySec
    wow_log_path = $WowLogPath
    llama_cli_bin = $LlamaCliBin
    model_path = $ModelPath
    binding_identity = $BindingIdentity
    max_tokens = $MaxTokens
    stimulus_prompt = $promptFile
}
Write-JsonNoBom -Path (Join-Path $runDir 'key-run-launch.json') -Value $launchInfo -Depth 8

Write-Output "[key] run_dir=$runDir"
Write-Output "[key] passive capture starts now; inference stimulus scheduled after $InferenceDelaySec sec"

$stimulusArgString = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-Command',
    (Quote-Arg ("Start-Sleep -Seconds $InferenceDelaySec; powershell.exe -NoProfile -ExecutionPolicy Bypass -File " +
        (Quote-Arg $stimulus) +
        " -RunDir " + (Quote-Arg $runDir) +
        " -Mode Cli" +
        " -LlamaCliBin " + (Quote-Arg $LlamaCliBin) +
        " -ModelPath " + (Quote-Arg $ModelPath) +
        " -VisibleDevices " + (Quote-Arg $VisibleDevices) +
        " -BindingIdentity " + (Quote-Arg $BindingIdentity) +
        " -Label " + (Quote-Arg "after-7m-qwen14b-cli") +
        " -PromptFile " + (Quote-Arg $promptFile) +
        " -MaxTokens $MaxTokens" +
        " *> " + (Quote-Arg $stimulusLog)))
) -join ' '

$stimulusProc = Start-Process -FilePath 'powershell.exe' -ArgumentList $stimulusArgString -PassThru -WindowStyle Hidden

$captureArgs = @{
    Scenario = 'wow-key-live'
    DurationSec = $DurationSec
    RunRoot = (Split-Path -Parent $runDir)
    FixedRunDir = $runDir
    TailWowLog = $true
    WowLogPath = $WowLogPath
    WowLogTailIntervalSec = 7
    Model = $ModelPath
    LlamaBuild = $LlamaCliBin
    GpuBinding = "GGML_VK_VISIBLE_DEVICES=$VisibleDevices"
}

try {
    & $startRun @captureArgs 2>&1 | Tee-Object -FilePath $outerLog
}
finally {
    if ($stimulusProc -and -not $stimulusProc.HasExited) {
        try { $stimulusProc | Stop-Process -Force -ErrorAction SilentlyContinue } catch {}
    }

    if (Test-Path $runDir) {
        powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $scriptRoot 'Summarize-WowImpactRun.ps1') -RunDir $runDir
    }
}
