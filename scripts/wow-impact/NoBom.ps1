function Ensure-ParentDirectory {
    param([Parameter(Mandatory=$true)] [string]$Path)

    $parent = Split-Path -Parent $Path
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
}

function Write-TextNoBom {
    param(
        [Parameter(Mandatory=$true)] [string]$Path,
        [Parameter(Mandatory=$true)] [string]$Content
    )

    Ensure-ParentDirectory -Path $Path
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Write-JsonNoBom {
    param(
        [Parameter(Mandatory=$true)] [string]$Path,
        [Parameter(Mandatory=$true)] $Value,
        [int]$Depth = 10,
        [switch]$Compress
    )

    $json = if ($Compress) {
        $Value | ConvertTo-Json -Depth $Depth -Compress
    } else {
        $Value | ConvertTo-Json -Depth $Depth
    }
    Write-TextNoBom -Path $Path -Content $json
}

function Append-JsonlNoBom {
    param(
        [Parameter(Mandatory=$true)] [string]$Path,
        [Parameter(Mandatory=$true)] $Value,
        [int]$Depth = 10
    )

    Ensure-ParentDirectory -Path $Path
    $line = ($Value | ConvertTo-Json -Depth $Depth -Compress)
    [System.IO.File]::AppendAllText($Path, $line + "`n", [System.Text.UTF8Encoding]::new($false))
}

function Test-Utf8NoBom {
    param([Parameter(Mandatory=$true)] [string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 3) { return $true }
    return -not ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
}
