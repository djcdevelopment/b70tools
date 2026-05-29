@echo off
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
set VC_VARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set CMAKE_PATH="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA_PATH="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

call %VC_VARS%
if errorlevel 1 ( echo failed to activate MSVC env 1>&2 & exit /b 2 )

if not exist "D:\work\b70tools\build\build.ninja" (
  %CMAKE_PATH% -G Ninja -DCMAKE_MAKE_PROGRAM=%NINJA_PATH% -DCMAKE_BUILD_TYPE=Release -S "D:\work\b70tools" -B "D:\work\b70tools\build"
  if errorlevel 1 exit /b %errorlevel%
)

%CMAKE_PATH% --build "D:\work\b70tools\build"
exit /b %errorlevel%
