@echo off
setlocal EnableDelayedExpansion

:: ============================================================
::  IDTXFlow GDExtension - Build All Platforms
::
::  Usage:
::    build_all.bat                  -- incremental build, all platforms
::    build_all.bat clean            -- clean all, then build all
::    build_all.bat windows          -- build only windows (debug+release)
::    build_all.bat android          -- build only android (debug+release)
::    build_all.bat web              -- build only web (debug+release)
::    build_all.bat windows clean    -- clean + build windows only
::
::  SDK paths - edit these to match your environment:
:: ============================================================

set EMSDK_ROOT=C:\Work\emscripten\emsdk
set ANDROID_NDK_ROOT=%LOCALAPPDATA%\Android\Sdk\ndk\27.2.12479018
set JOBS=8

:: ANSI colors (Windows 10+ / modern terminal)
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "C0=%ESC%[0m"
set "CBOLD=%ESC%[1m"
set "CGREEN=%ESC%[92m"
set "CRED=%ESC%[91m"
set "CCYAN=%ESC%[96m"
set "CYELLOW=%ESC%[93m"

set FAILED_BUILDS=
set FAILED_COUNT=0
set TOTAL_COUNT=0

:: ============================================================
::  Parse arguments
:: ============================================================

set DO_CLEAN=0
set PLATFORM_FILTER=all

for %%A in (%*) do (
    if /I "%%A"=="clean"   set DO_CLEAN=1
    if /I "%%A"=="windows" set PLATFORM_FILTER=windows
    if /I "%%A"=="android" set PLATFORM_FILTER=android
    if /I "%%A"=="web"     set PLATFORM_FILTER=web
)

goto :main

:: ============================================================
::  :banner_start  -- prints STARTING banner using %_LABEL%
::  :banner_end    -- prints FINISHED/FAILED based on %_EC%,
::                    records failures for the final summary
:: ============================================================

:banner_start
echo.
echo %CCYAN%%CBOLD%============================================================%C0%
echo %CCYAN%%CBOLD%   STARTING: %_LABEL%%C0%
echo %CCYAN%%CBOLD%============================================================%C0%
set /a TOTAL_COUNT+=1
exit /b 0

:banner_end
if "%_EC%"=="0" (
    echo %CGREEN%%CBOLD%============================================================%C0%
    echo %CGREEN%%CBOLD%   FINISHED: %_LABEL%%C0%
    echo %CGREEN%%CBOLD%============================================================%C0%
) else (
    echo %CRED%%CBOLD%============================================================%C0%
    echo %CRED%%CBOLD%   FAILED:   %_LABEL%%C0%
    echo %CRED%%CBOLD%============================================================%C0%
    set /a FAILED_COUNT+=1
    if "!FAILED_BUILDS!"=="" (
        set "FAILED_BUILDS=!_LABEL!"
    ) else (
        set "FAILED_BUILDS=!FAILED_BUILDS!, !_LABEL!"
    )
)
exit /b 0

:do_clean
echo %CYELLOW%[CLEAN] scons %* -c%C0%
scons %* -c
exit /b 0

:: ============================================================
::  Main
:: ============================================================
:main

echo.
echo %CBOLD%============================================================%C0%
echo %CBOLD%  IDTXFlow Build All Platforms%C0%
echo %CBOLD%  Platform filter : %PLATFORM_FILTER%%C0%
echo %CBOLD%  Clean first     : %DO_CLEAN%%C0%
echo %CBOLD%  Parallel jobs   : %JOBS%%C0%
echo %CBOLD%============================================================%C0%

:: ---- Windows ------------------------------------------------
if "%PLATFORM_FILTER%"=="all"     goto :build_windows
if "%PLATFORM_FILTER%"=="windows" goto :build_windows
goto :skip_windows

:build_windows
if "%DO_CLEAN%"=="1" (
    call :do_clean platform=windows target=template_debug
    call :do_clean platform=windows target=template_release
)

set "_LABEL=WINDOWS / DEBUG"
call :banner_start
scons platform=windows target=template_debug -j%JOBS%
set _EC=%ERRORLEVEL%
call :banner_end

set "_LABEL=WINDOWS / RELEASE"
call :banner_start
scons platform=windows target=template_release -j%JOBS%
set _EC=%ERRORLEVEL%
call :banner_end

:skip_windows

:: ---- Android ------------------------------------------------
if "%PLATFORM_FILTER%"=="all"     goto :build_android
if "%PLATFORM_FILTER%"=="android" goto :build_android
goto :skip_android

:build_android
if not exist "%ANDROID_NDK_ROOT%" (
    echo %CYELLOW%[SKIP] Android NDK not found: %ANDROID_NDK_ROOT%%C0%
    goto :skip_android
)
if "%DO_CLEAN%"=="1" (
    call :do_clean platform=android target=template_debug   ANDROID_NDK_ROOT="%ANDROID_NDK_ROOT%"
    call :do_clean platform=android target=template_release ANDROID_NDK_ROOT="%ANDROID_NDK_ROOT%"
)

set "_LABEL=ANDROID / DEBUG"
call :banner_start
scons platform=android target=template_debug ANDROID_NDK_ROOT="%ANDROID_NDK_ROOT%" -j%JOBS%
set _EC=%ERRORLEVEL%
call :banner_end

set "_LABEL=ANDROID / RELEASE"
call :banner_start
scons platform=android target=template_release ANDROID_NDK_ROOT="%ANDROID_NDK_ROOT%" -j%JOBS%
set _EC=%ERRORLEVEL%
call :banner_end

:skip_android

:: ---- Web (wasm32) -------------------------------------------
if "%PLATFORM_FILTER%"=="all" goto :build_web
if "%PLATFORM_FILTER%"=="web"  goto :build_web
goto :skip_web

:build_web
if not exist "%EMSDK_ROOT%" (
    echo %CYELLOW%[SKIP] EMSDK not found: %EMSDK_ROOT%%C0%
    goto :skip_web
)
if "%DO_CLEAN%"=="1" (
    call :do_clean platform=wasm32 target=template_debug   EMSDK_ROOT="%EMSDK_ROOT%"
    call :do_clean platform=wasm32 target=template_release EMSDK_ROOT="%EMSDK_ROOT%"
)

set "_LABEL=WEB / DEBUG"
call :banner_start
scons platform=wasm32 target=template_debug EMSDK_ROOT="%EMSDK_ROOT%" -j%JOBS%
set _EC=%ERRORLEVEL%
call :banner_end

set "_LABEL=WEB / RELEASE"
call :banner_start
scons platform=wasm32 target=template_release EMSDK_ROOT="%EMSDK_ROOT%" -j%JOBS%
set _EC=%ERRORLEVEL%
call :banner_end

:skip_web

:: ---- Summary ------------------------------------------------
echo.
echo %CBOLD%============================================================%C0%
echo %CBOLD%  BUILD SUMMARY  (%TOTAL_COUNT% builds attempted)%C0%
echo %CBOLD%============================================================%C0%
if "%FAILED_COUNT%"=="0" (
    echo %CGREEN%%CBOLD%  All builds passed!%C0%
) else (
    echo %CRED%%CBOLD%  %FAILED_COUNT% build^(s^) failed: %FAILED_BUILDS%%C0%
)
echo %CBOLD%============================================================%C0%
echo.

if not "%FAILED_COUNT%"=="0" exit /b 1
endlocal
exit /b 0
