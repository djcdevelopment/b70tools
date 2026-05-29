@echo off
REM Convenience wrapper: activates MSVC env, then runs cmake build.
REM Used by the eval harness so we have a single-shot build invocation.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [build-and-test] failed to activate MSVC env 1>&2
    exit /b 2
)
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build "D:\work\b70tools\build" %*
exit /b %ERRORLEVEL%
