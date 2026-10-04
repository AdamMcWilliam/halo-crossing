@echo off
rem Windows entry point: runs build.sh inside MSYS2's MINGW32 environment.
rem   build.bat [build|run|test|clean]
rem Set MSYS2_ROOT if MSYS2 is not installed at C:\msys64.
setlocal
cd /d "%~dp0"
if "%MSYS2_ROOT%"=="" set "MSYS2_ROOT=C:\msys64"
if not exist "%MSYS2_ROOT%\usr\bin\bash.exe" (
    echo MSYS2 not found at %MSYS2_ROOT%. Install it from https://www.msys2.org/ or set MSYS2_ROOT.
    exit /b 1
)
set MSYSTEM=MINGW32
set CHERE_INVOKING=1
rem MSYS2's own tools stay first; Git for Windows is found if MSYS2 has no git.
if "%MSYS2_PATH_TYPE%"=="" set MSYS2_PATH_TYPE=inherit
"%MSYS2_ROOT%\usr\bin\bash.exe" -lc "./build.sh %*"
exit /b %ERRORLEVEL%
