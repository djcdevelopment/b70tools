param(
    [Parameter(Mandatory=$true)] [string]$OutPath,
    [double]$IntervalSec = 1.0,
    [string]$ProcessNames = 'Wow,WowT,WowClassic,llama-server,llama-cli,ollama,dotnet,Tempo.Host'
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'NoBom.ps1')

$outDir = Split-Path -Parent $OutPath
if ($outDir) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

function Get-PageFileUsageMb {
    try {
        $items = @(Get-CimInstance Win32_PageFileUsage -ErrorAction Stop)
        if ($items.Count -eq 0) { return 0 }
        return [math]::Round((($items | Measure-Object -Property CurrentUsage -Sum).Sum), 2)
    } catch {
        return $null
    }
}

function Get-MemorySnapshot {
    try {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        $totalGb = [math]::Round($os.TotalVisibleMemorySize / 1MB, 3)
        $freeGb = [math]::Round($os.FreePhysicalMemory / 1MB, 3)
        return [pscustomobject]@{ total_gb = $totalGb; free_gb = $freeGb }
    } catch {
        Add-Type -AssemblyName Microsoft.VisualBasic -ErrorAction SilentlyContinue
        $ci = New-Object Microsoft.VisualBasic.Devices.ComputerInfo
        $totalGb = [math]::Round($ci.TotalPhysicalMemory / 1GB, 3)
        $freeGb = [math]::Round($ci.AvailablePhysicalMemory / 1GB, 3)
        return [pscustomobject]@{ total_gb = $totalGb; free_gb = $freeGb }
    }
}

function Get-DiskActivePct {
    try {
        $sample = Get-Counter '\PhysicalDisk(_Total)\% Disk Time' -ErrorAction Stop
        return [math]::Round([double]$sample.CounterSamples[0].CookedValue, 2)
    } catch {
        return $null
    }
}

function Get-CommitSnapshot {
    try {
        $sample = Get-Counter @('\Memory\Committed Bytes', '\Memory\Commit Limit') -ErrorAction Stop
        $committed = ($sample.CounterSamples | Where-Object { $_.Path -like '*\memory\committed bytes' } | Select-Object -First 1).CookedValue
        $limit = ($sample.CounterSamples | Where-Object { $_.Path -like '*\memory\commit limit' } | Select-Object -First 1).CookedValue
        return [pscustomobject]@{
            committed_gb = [math]::Round([double]$committed / 1GB, 3)
            commit_limit_gb = [math]::Round([double]$limit / 1GB, 3)
        }
    } catch {
        return [pscustomobject]@{ committed_gb = $null; commit_limit_gb = $null }
    }
}

$processNameList = @($ProcessNames -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })

try {
    while ($true) {
        $mem = Get-MemorySnapshot
        $commit = Get-CommitSnapshot
        $totalGb = $mem.total_gb
        $freeGb = $mem.free_gb
        $usedGb = [math]::Round($totalGb - $freeGb, 3)

        $seen = New-Object System.Collections.ArrayList
        foreach ($name in $processNameList) {
            $procs = @(Get-Process -Name $name -ErrorAction SilentlyContinue)
            foreach ($p in $procs) {
                [void]$seen.Add([ordered]@{
                    name = $p.ProcessName
                    pid = $p.Id
                    cpu_s = if ($null -ne $p.CPU) { [math]::Round([double]$p.CPU, 3) } else { $null }
                    ws_mb = [math]::Round($p.WorkingSet64 / 1MB, 2)
                    pm_mb = [math]::Round($p.PagedMemorySize64 / 1MB, 2)
                    private_mb = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
                })
            }
        }

        $record = [ordered]@{
            ts = (Get-Date).ToString('o')
            total_ram_gb = $totalGb
            free_ram_gb = $freeGb
            used_ram_gb = $usedGb
            committed_gb = $commit.committed_gb
            commit_limit_gb = $commit.commit_limit_gb
            pagefile_current_mb = Get-PageFileUsageMb
            disk_active_pct = Get-DiskActivePct
            processes = @($seen)
        }

        Append-JsonlNoBom -Path $OutPath -Value $record -Depth 8
        Start-Sleep -Milliseconds ([int]($IntervalSec * 1000))
    }
}
finally {}
