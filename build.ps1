$ErrorActionPreference = 'Stop'

$VsRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community"
$VcVars = "$VsRoot\VC\Auxiliary\Build\vcvars64.bat"
$CMake  = "$VsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$Ninja  = "$VsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if (-not (Test-Path $VcVars)) { throw "vcvars64.bat not found at $VcVars" }
if (-not (Test-Path $CMake))  { throw "cmake not found at $CMake" }

cmd /c "`"$VcVars`" >NUL 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2]
    }
}

$Root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root 'build'
New-Item -ItemType Directory -Force -Path $Build | Out-Null

& $CMake -S $Root -B $Build -G Ninja `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo `
    -DCMAKE_MAKE_PROGRAM="$Ninja" `
    -DCMAKE_C_COMPILER=cl `
    -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

& $CMake --build $Build
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Output ""
Write-Output "Build output: $Build\b70tools.exe"
