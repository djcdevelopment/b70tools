param(
    [Parameter(Mandatory=$true)] [string]$OutPath,
    [double]$IntervalSec = 1.0
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NoBom.ps1')

function Get-CounterValue {
    param([Parameter(Mandatory=$true)] [string[]]$Paths)

    foreach ($path in $Paths) {
        try {
            $sample = Get-Counter -Counter $path -ErrorAction Stop
            if ($sample.CounterSamples -and $sample.CounterSamples.Count -gt 0) {
                return [double]$sample.CounterSamples[0].CookedValue
            }
        } catch {
        }
    }
    return $null
}

Append-JsonlNoBom -Path $OutPath -Value ([ordered]@{
    ts = (Get-Date).ToString('o')
    kind = 'dpc_start'
    interval_sec = $IntervalSec
}) -Depth 6

while ($true) {
    $now = Get-Date
    $record = [ordered]@{
        ts = $now.ToString('o')
        kind = 'dpc_sample'
        interval_sec = $IntervalSec
        dpc_pct = Get-CounterValue -Paths @(
            '\Processor(_Total)\% DPC Time',
            '\Processor Information(_Total)\% DPC Time'
        )
        interrupt_pct = Get-CounterValue -Paths @(
            '\Processor(_Total)\% Interrupt Time',
            '\Processor Information(_Total)\% Interrupt Time'
        )
        queue_length = Get-CounterValue -Paths @(
            '\System\Processor Queue Length'
        )
    }

    if ($null -eq $record.dpc_pct -and $null -eq $record.interrupt_pct -and $null -eq $record.queue_length) {
        $record.kind = 'dpc_error'
        $record.error = 'no DPC/interrupt counters available'
    }

    Append-JsonlNoBom -Path $OutPath -Value $record -Depth 6
    Start-Sleep -Milliseconds ([int]($IntervalSec * 1000))
}
