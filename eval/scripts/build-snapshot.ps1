# build-snapshot.ps1
# Generates a benchmark snapshot of a target repository per eval/snapshot-policy.md.
#
# Usage:
#   .\build-snapshot.ps1 [-TargetDir <path>] [-OutFile <path>] [-TargetBytes <int>] [-ListOnly]
#
# Defaults: target = D:\work\battlemage, output = eval/snapshots/<timestamp>.md, budget = 48 KB.
# Token-budgeting is approximated as 4 bytes per token; aim 48 KB ≈ 12K tokens.

param(
    [string]$TargetDir   = "D:\work\battlemage",
    [string]$OutFile     = "",
    [int]   $TargetBytes = 75000,  # ~22K tokens at ~3.5 B/token English+code; leaves headroom for Round-3 transcripts in a 32K context
    [switch]$ListOnly
)

$ErrorActionPreference = 'Stop'
$ScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$EvalRoot   = Split-Path -Parent $ScriptRoot

if (-not $OutFile) {
    $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $snapDir = Join-Path $EvalRoot 'snapshots'
    if (-not (Test-Path $snapDir)) { New-Item -ItemType Directory -Force -Path $snapDir | Out-Null }
    $OutFile = Join-Path $snapDir "snapshot-$stamp.md"
}

if (-not (Test-Path $TargetDir)) {
    throw "Target dir not found: $TargetDir"
}

# --- inclusion / exclusion policy (mirrors snapshot-policy.md) -----------------
$AnchorDocs = @(
    'README.md', 'AGENTS.md', 'PROJECT_CONTEXT.md', 'RESEARCH_LINK.md', 'SNAPSHOT_CONTRACT.md',
    'arc-b70-dual-70b-windows-vulkan.md',
    'bench-2026-05-24.ps1', 'bench-2026-05-24.stdout.log', 'bench-2026-05-24.stderr.log',
    'ollama-vulkan-smoke.log', 'ollama-vulkan-smoke.log.err'
)

# Subdirectories whose CONTENTS are elided but tree-presence preserved
$VendorBinaryDirs = @(
    'llamacpp-win-vulkan',   # bundled llama.cpp Vulkan release binaries
    'sd-cpp-win-vulkan',     # bundled stable-diffusion.cpp binaries
    'models'                 # GGUF/model weights
)

# Filename extensions never included as content (always elide)
$BinaryExts = @('.dll', '.exe', '.lib', '.pdb', '.obj', '.gguf', '.safetensors',
                '.onnx', '.bin', '.zip', '.tar', '.gz', '.7z', '.png', '.jpg', '.jpeg',
                '.gif', '.ico', '.pdf')

# File-content size cap per included file (truncate head+tail above this)
$PerFileBytesCap = 6144   # ~6 KB ≈ 1.5K tokens

# Extensions that are inherently small + high-signal — always include all of them per dir,
# subject to the global byte budget. These represent the "chaos surface" most clearly:
# log artifacts, benchmark results, config files, etc.
$AlwaysIncludeExts = @('.log', '.err', '.jsonl', '.json', '.cmd', '.bat')

# Small-dir threshold — if a top-level dir has at most this many files, include ALL of them.
$SmallDirThreshold = 12

# Source-shaped extensions (used for selecting representative code files per dir)
$SourceExts = @('.py', '.ps1', '.sh', '.bat', '.cmd', '.cs', '.cpp', '.cc', '.c', '.h',
                '.hpp', '.rs', '.go', '.ts', '.tsx', '.js', '.jsx', '.json', '.yml',
                '.yaml', '.toml', '.ini', '.cfg', '.conf', '.md', '.txt', '.log',
                '.html', '.css')

# --- tree walking --------------------------------------------------------------
function Is-VendorBinaryPath {
    param([string]$RelPath)
    foreach ($vd in $VendorBinaryDirs) {
        if ($RelPath -eq $vd -or $RelPath -like "$vd/*" -or $RelPath -like "$vd\*") { return $true }
    }
    return $false
}

function Get-RelPath {
    param([string]$Full, [string]$Root)
    $rel = $Full.Substring($Root.Length).TrimStart('\','/')
    return $rel -replace '\\','/'
}

# Enumerate all files under target
$allFiles = Get-ChildItem -Path $TargetDir -Recurse -File -Force 2>$null
$treeEntries = New-Object System.Collections.Generic.List[string]
$includedFiles = New-Object System.Collections.Generic.List[object]
$elidedFiles   = New-Object System.Collections.Generic.List[object]
$truncatedFiles= New-Object System.Collections.Generic.List[object]

foreach ($f in $allFiles) {
    $rel = Get-RelPath -Full $f.FullName -Root $TargetDir
    $treeEntries.Add($rel)
}

# Sort tree alphabetically for determinism
$sortedTree = $treeEntries | Sort-Object

# Build a display tree that collapses vendor binary dirs into aggregate lines.
# Tree-presence is preserved (the dir + count appears) without listing every binary path.
$displayTree = New-Object System.Collections.Generic.List[string]
$vendorCounts = @{}
foreach ($vd in $VendorBinaryDirs) { $vendorCounts[$vd] = 0 }
$shownVendorRoots = New-Object System.Collections.Generic.HashSet[string]
foreach ($t in $sortedTree) {
    $isVendor = $false
    foreach ($vd in $VendorBinaryDirs) {
        if ($t -eq $vd -or $t -like ($vd + '/*') -or $t -like ($vd + '\*')) {
            $vendorCounts[$vd] = $vendorCounts[$vd] + 1
            if (-not $shownVendorRoots.Contains($vd)) {
                $shownVendorRoots.Add($vd) | Out-Null
                # placeholder; filled after pass with final count
                $displayTree.Add('___VENDOR_PLACEHOLDER___' + $vd)
            }
            $isVendor = $true
            break
        }
    }
    if (-not $isVendor) {
        $displayTree.Add($t)
    }
}
# Replace placeholders with final counts
$finalDisplayTree = New-Object System.Collections.Generic.List[string]
foreach ($d in $displayTree) {
    if ($d.StartsWith('___VENDOR_PLACEHOLDER___')) {
        $vd = $d.Substring('___VENDOR_PLACEHOLDER___'.Length)
        $line = $vd + '/   [' + $vendorCounts[$vd] + ' files elided — vendor binary directory]'
        $finalDisplayTree.Add($line)
    } else {
        $finalDisplayTree.Add($d)
    }
}

# --- pick included content per policy ------------------------------------------
$selected = @()   # ordered list of @{ rel = ...; full = ...; size = ...; reason = ... }
$bytesUsed = 0

# Step 1: anchor docs (verbatim, but respect per-file cap)
foreach ($docName in $AnchorDocs) {
    $candidate = $allFiles | Where-Object {
        (Get-RelPath -Full $_.FullName -Root $TargetDir) -eq $docName
    } | Select-Object -First 1
    if ($candidate) {
        $sz = [int]$candidate.Length
        $selected += [pscustomobject]@{
            rel    = (Get-RelPath -Full $candidate.FullName -Root $TargetDir)
            full   = $candidate.FullName
            size   = $sz
            reason = 'anchor-doc'
        }
    }
}

# Step 2: per-dir interleaved selection — every non-vendor dir gets its allotment of slots
# BEFORE any single dir saturates the budget. Each dir contributes a ranked list of candidates;
# we then interleave round-robin so every dir's top picks are written first.

# Per-dir token allotment (number of candidate slots reserved per dir before interleaving).
$PerDirSlotCount = 10

$dirCandidateLists = @{}
$topDirs = Get-ChildItem -Path $TargetDir -Directory -Force 2>$null | Sort-Object Name
foreach ($d in $topDirs) {
    $dirRel = $d.Name
    if (Is-VendorBinaryPath -RelPath $dirRel) { continue }

    $dirCandidates = @()

    if ($dirRel -eq '.claude' -or $dirRel -eq '.git') {
        $tr = Get-ChildItem -Path $d.FullName -Recurse -File -Force 2>$null |
              Where-Object { $_.Length -le $PerFileBytesCap } |
              Sort-Object Length |
              Select-Object -First 3
        foreach ($c in $tr) {
            $dirCandidates += [pscustomobject]@{
                rel    = (Get-RelPath -Full $c.FullName -Root $TargetDir)
                full   = $c.FullName
                size   = [int]$c.Length
                reason = "$dirRel-trace"
            }
        }
        $dirCandidateLists[$dirRel] = $dirCandidates
        continue
    }

    $allFilesInDir = Get-ChildItem -Path $d.FullName -Recurse -File -Force 2>$null

    if ($allFilesInDir.Count -le $SmallDirThreshold) {
        foreach ($c in ($allFilesInDir | Sort-Object Name)) {
            $ext = [System.IO.Path]::GetExtension($c.Name).ToLowerInvariant()
            if ($BinaryExts -contains $ext) { continue }
            $dirCandidates += [pscustomobject]@{
                rel    = (Get-RelPath -Full $c.FullName -Root $TargetDir)
                full   = $c.FullName
                size   = [int]$c.Length
                reason = "small-dir-$dirRel"
            }
        }
        $dirCandidateLists[$dirRel] = $dirCandidates
        continue
    }

    $sourceCandidates = $allFilesInDir |
                        Where-Object {
                            $ext = [System.IO.Path]::GetExtension($_.Name).ToLowerInvariant()
                            $SourceExts -contains $ext -and $BinaryExts -notcontains $ext
                        }

    # Tier 1: artifacts (log/err/jsonl/json/cmd/bat) — sorted smallest first, take a few
    $artifacts = $sourceCandidates | Where-Object {
        $ext = [System.IO.Path]::GetExtension($_.Name).ToLowerInvariant()
        $AlwaysIncludeExts -contains $ext
    } | Sort-Object Length | Select-Object -First 8
    foreach ($c in $artifacts) {
        $dirCandidates += [pscustomobject]@{
            rel    = (Get-RelPath -Full $c.FullName -Root $TargetDir)
            full   = $c.FullName
            size   = [int]$c.Length
            reason = "artifact-in-$dirRel"
        }
    }

    # Tier 2: entry-shaped scripts
    $entryShaped = $sourceCandidates | Where-Object {
        $n = $_.Name.ToLowerInvariant()
        ($n -match '^(main|index|run|cli|server|app|setup|config|schema|api|build|bench)\.') -or
        ($n -eq 'requirements.txt') -or ($n -eq 'package.json') -or ($n -eq 'cargo.toml') -or
        ($n -eq 'cmakelists.txt') -or ($n -like '*.ps1')
    } | Where-Object { $artifacts -notcontains $_ } | Sort-Object Length | Select-Object -First 4
    foreach ($c in $entryShaped) {
        $dirCandidates += [pscustomobject]@{
            rel    = (Get-RelPath -Full $c.FullName -Root $TargetDir)
            full   = $c.FullName
            size   = [int]$c.Length
            reason = "entry-in-$dirRel"
        }
    }

    # Tier 3: representative samples
    $samples = $sourceCandidates |
               Where-Object { $artifacts -notcontains $_ -and $entryShaped -notcontains $_ } |
               Sort-Object Length |
               Select-Object -First 6
    foreach ($c in $samples) {
        $dirCandidates += [pscustomobject]@{
            rel    = (Get-RelPath -Full $c.FullName -Root $TargetDir)
            full   = $c.FullName
            size   = [int]$c.Length
            reason = "sample-in-$dirRel"
        }
    }

    $dirCandidateLists[$dirRel] = $dirCandidates
}

# Interleave: round-robin one file from each dir until exhausted.
$dirKeys = @($dirCandidateLists.Keys | Sort-Object)
$maxLen = 0
foreach ($k in $dirKeys) {
    if ($dirCandidateLists[$k].Count -gt $maxLen) { $maxLen = $dirCandidateLists[$k].Count }
}
for ($i = 0; $i -lt $maxLen; $i++) {
    foreach ($k in $dirKeys) {
        $list = $dirCandidateLists[$k]
        if ($i -lt $list.Count) {
            $selected += $list[$i]
        }
    }
}

# --- write the snapshot --------------------------------------------------------
$sb = New-Object System.Text.StringBuilder
$null = $sb.AppendLine('# Repository Snapshot')
$null = $sb.AppendLine('')
$null = $sb.AppendLine("Target: $TargetDir")
$null = $sb.AppendLine("Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')")
$null = $sb.AppendLine("Token budget (approx, 4 B/token): ${TargetBytes} B ≈ $([int]($TargetBytes/4)) tokens")
$null = $sb.AppendLine('')
$null = $sb.AppendLine('This snapshot was generated per `eval/snapshot-policy.md`. It is intended to be the')
$null = $sb.AppendLine('complete evidence surface available to the evaluated model. Operator-facing docs, runbooks,')
$null = $sb.AppendLine('agent contracts, scripts, and log artifacts are included verbatim where size permits.')
$null = $sb.AppendLine('Vendor-bundled binary directories, model weights, and binary file extensions are elided')
$null = $sb.AppendLine('with tree-presence preserved.')
$null = $sb.AppendLine('')
$null = $sb.AppendLine('---')
$null = $sb.AppendLine('')
$null = $sb.AppendLine('## File Tree')
$null = $sb.AppendLine('')
$null = $sb.AppendLine('```')
# Emit an indented tree to save space vs. repeating the full path on every line.
$prevSegments = @()
foreach ($t in $finalDisplayTree) {
    # Annotated vendor lines look like "llamacpp-win-vulkan/   [53 files elided ...]"
    # Treat them as top-level entries.
    if ($t -match '^([^/\\]+)/\s+\[') {
        $null = $sb.AppendLine($t)
        $prevSegments = @()
        continue
    }
    # Normalize separators then split
    $normalized = $t -replace '\\', '/'
    $segs = $normalized.Split('/')
    $fileName = $segs[-1]
    if ($segs.Count -gt 1) {
        $dirSegs = $segs[0..($segs.Count - 2)]
    } else {
        $dirSegs = @()
    }

    # Find common prefix length with previous path's dir segments
    $common = 0
    $limit = [Math]::Min($prevSegments.Count, $dirSegs.Count)
    while ($common -lt $limit -and $prevSegments[$common] -eq $dirSegs[$common]) {
        $common++
    }

    # Emit any new directory segments
    for ($i = $common; $i -lt $dirSegs.Count; $i++) {
        $indent = '  ' * $i
        $null = $sb.AppendLine($indent + $dirSegs[$i] + '/')
    }

    # Emit the file at the appropriate depth
    $fileIndent = '  ' * $dirSegs.Count
    $null = $sb.AppendLine($fileIndent + $fileName)

    $prevSegments = $dirSegs
}
$null = $sb.AppendLine('```')
$null = $sb.AppendLine('')
$null = $sb.AppendLine('---')
$null = $sb.AppendLine('')
$null = $sb.AppendLine('## Included File Contents')
$null = $sb.AppendLine('')

foreach ($item in $selected) {
    if ($bytesUsed -ge $TargetBytes) {
        $elidedFiles.Add([pscustomobject]@{ rel = $item.rel; size = $item.size; reason = 'budget-exceeded' })
        continue
    }
    $remaining = $TargetBytes - $bytesUsed
    $cap = [Math]::Min($PerFileBytesCap, $remaining)
    try {
        $content = Get-Content -Raw -LiteralPath $item.full -Encoding UTF8 -ErrorAction Stop
    } catch {
        try { $content = Get-Content -Raw -LiteralPath $item.full -ErrorAction Stop }
        catch { $content = "[unable to read file: $_]" }
    }
    if ($null -eq $content) { $content = '' }

    $truncated = $false
    $contentBytes = [System.Text.Encoding]::UTF8.GetByteCount($content)
    if ($contentBytes -gt $cap) {
        $half = [int]($cap / 2)
        $head = $content.Substring(0, [Math]::Min($half, $content.Length))
        $tail = ''
        if ($content.Length -gt ($cap - $half)) {
            $tailStart = [Math]::Max(0, $content.Length - ($cap - $half))
            $tail = $content.Substring($tailStart)
        }
        $content = "$head`n`n[...truncated $($contentBytes - $cap) bytes...]`n`n$tail"
        $truncated = $true
        $truncatedFiles.Add([pscustomobject]@{
            rel = $item.rel; original_size = $item.size; truncated_to = $cap
        })
    }

    $ext = [System.IO.Path]::GetExtension($item.rel).TrimStart('.').ToLowerInvariant()
    $lang = switch ($ext) {
        'ps1' { 'powershell' }
        'py'  { 'python' }
        'cs'  { 'csharp' }
        'cpp' { 'cpp' }
        'cc'  { 'cpp' }
        'h'   { 'cpp' }
        'hpp' { 'cpp' }
        'rs'  { 'rust' }
        'go'  { 'go' }
        'js'  { 'javascript' }
        'ts'  { 'typescript' }
        'tsx' { 'typescript' }
        'json' { 'json' }
        'yml' { 'yaml' }
        'yaml' { 'yaml' }
        'toml' { 'toml' }
        'sh'  { 'bash' }
        'bat' { 'batch' }
        'cmd' { 'batch' }
        default { '' }
    }

    $truncFlag = ''
    if ($truncated) { $truncFlag = '; truncated' }
    $headerLine = '### `' + $item.rel + '`'
    $metaLine   = '(reason: ' + $item.reason + '; original size: ' + $item.size + ' B' + $truncFlag + ')'
    $null = $sb.AppendLine($headerLine)
    $null = $sb.AppendLine($metaLine)
    $null = $sb.AppendLine('')
    $null = $sb.AppendLine('```' + $lang)
    $null = $sb.AppendLine($content)
    $null = $sb.AppendLine('```')
    $null = $sb.AppendLine('')

    $bytesUsed += [System.Text.Encoding]::UTF8.GetByteCount($content)
    $includedFiles.Add([pscustomobject]@{ rel = $item.rel; size = $item.size; reason = $item.reason; truncated = $truncated })
}

# Manifest of what was excluded
$elidedDirs = @()
foreach ($vd in $VendorBinaryDirs) {
    $globA = $vd + '/*'
    $globB = $vd + '\*'
    $dirCount = ($sortedTree | Where-Object { ($_ -like $globA) -or ($_ -like $globB) }).Count
    if ($dirCount -gt 0) {
        $elidedDirs += [pscustomobject]@{ dir = $vd; files_elided = $dirCount; reason = 'vendor-binary-dir' }
    }
}

$null = $sb.AppendLine('---')
$null = $sb.AppendLine('')
$null = $sb.AppendLine('## Manifest')
$null = $sb.AppendLine('')
$null = $sb.AppendLine("Bytes used: $bytesUsed / $TargetBytes")
$null = $sb.AppendLine("Files included: $($includedFiles.Count)")
$null = $sb.AppendLine("Files truncated: $($truncatedFiles.Count)")
$null = $sb.AppendLine("Files elided (over budget): $($elidedFiles.Count)")
$null = $sb.AppendLine("Directories elided wholesale: $($elidedDirs.Count)")
$null = $sb.AppendLine('')
if ($elidedDirs.Count -gt 0) {
    $null = $sb.AppendLine('Directories elided:')
    foreach ($d in $elidedDirs) {
        $line = '  - ' + $d.dir + ': ' + $d.files_elided + ' files (' + $d.reason + ')'
        $null = $sb.AppendLine($line)
    }
    $null = $sb.AppendLine('')
}

# Write to file
$sb.ToString() | Out-File -FilePath $OutFile -Encoding UTF8 -Force
$finalSize = (Get-Item $OutFile).Length

Write-Output "Snapshot written: $OutFile"
Write-Output "  Final size: $finalSize B (~$([int]($finalSize/4)) tokens approx)"
Write-Output "  Tree entries: $($sortedTree.Count)"
Write-Output "  Files included verbatim: $($includedFiles.Count)"
Write-Output "  Files truncated: $($truncatedFiles.Count)"
Write-Output "  Files over budget (skipped): $($elidedFiles.Count)"
Write-Output "  Vendor-binary dirs elided: $($elidedDirs.Count)"

if ($ListOnly) {
    Write-Output ""
    Write-Output "Included files:"
    foreach ($it in $includedFiles) {
        $msg = "  - " + $it.rel + " (" + $it.reason + ")"
        Write-Output $msg
    }
}
