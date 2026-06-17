param(
    [Parameter(Mandatory=$true)] [string]$LogPath,
    [Parameter(Mandatory=$true)] [string]$OutPath,
    [double]$IntervalSec = 7.0,
    [int]$LastLineChars = 500,
    [switch]$StartAtBeginning
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NoBom.ps1')

$outDir = Split-Path -Parent $OutPath
if ($outDir) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

function Read-NewText {
    param(
        [Parameter(Mandatory=$true)] [string]$Path,
        [Parameter(Mandatory=$true)] [long]$Offset
    )

    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        if ($Offset -gt $fs.Length) {
            $Offset = 0
        }
        [void]$fs.Seek($Offset, [System.IO.SeekOrigin]::Begin)
        $remaining = [int]($fs.Length - $Offset)
        if ($remaining -le 0) {
            return [pscustomobject]@{ text = ''; next_offset = $fs.Length; file_length = $fs.Length }
        }
        $buffer = New-Object byte[] $remaining
        $read = $fs.Read($buffer, 0, $remaining)
        $text = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $read)
        return [pscustomobject]@{ text = $text; next_offset = $Offset + $read; file_length = $fs.Length }
    }
    finally {
        $fs.Close()
    }
}

if (-not (Test-Path $LogPath)) {
    throw "WoW combat log not found: $LogPath"
}

$initial = Get-Item -LiteralPath $LogPath
$offset = if ($StartAtBeginning) { 0L } else { [long]$initial.Length }

try {
    Append-JsonlNoBom -Path $OutPath -Value ([ordered]@{
        ts = (Get-Date).ToString('o')
        kind = 'tail_start'
        log_path = $LogPath
        start_offset = $offset
        file_length = [long]$initial.Length
        start_at_beginning = [bool]$StartAtBeginning
    }) -Depth 6

    while ($true) {
        $sampleStart = Get-Date
        $record = [ordered]@{
            ts = $sampleStart.ToString('o')
            kind = 'tail_sample'
            log_path = $LogPath
            start_offset = $offset
            end_offset = $offset
            file_length = $null
            delta_bytes = 0
            delta_lines = 0
            last_line = $null
        }

        try {
            if (-not (Test-Path $LogPath)) {
                $record.kind = 'tail_missing'
            } else {
                $chunk = Read-NewText -Path $LogPath -Offset $offset
                $offset = [long]$chunk.next_offset
                $record.end_offset = $offset
                $record.file_length = [long]$chunk.file_length
                $record.delta_bytes = [math]::Max(0, [long]$record.end_offset - [long]$record.start_offset)

                if ($chunk.text) {
                    $lines = @($chunk.text -split "`r?`n" | Where-Object { $_ -ne '' })
                    $record.delta_lines = $lines.Count
                    if ($lines.Count -gt 0) {
                        $last = [string]$lines[-1]
                        if ($last.Length -gt $LastLineChars) {
                            $last = $last.Substring($last.Length - $LastLineChars)
                        }
                        $record.last_line = $last
                    }
                }
            }
        } catch {
            $record.kind = 'tail_error'
            $record.error = $_.Exception.Message
        }

        Append-JsonlNoBom -Path $OutPath -Value $record -Depth 6
        Start-Sleep -Milliseconds ([int]($IntervalSec * 1000))
    }
}
finally {}
