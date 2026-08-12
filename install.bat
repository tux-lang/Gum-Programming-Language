@echo off
setlocal enabledelayedexpansion

title Gum Installer

set "APP_DIR=%~dp0"
set "APP_DIR=%APP_DIR:~0,-1%"
set "SEKC_EXE=%APP_DIR%\sekc.exe"

if not exist "%SEKC_EXE%" (
    echo [X] sekc.exe not found in %APP_DIR%
    pause
    exit /b 1
)

echo ============================================
echo   Gum Installer
echo ============================================
echo.
echo Installing from: %APP_DIR%
echo.

fltmc >nul 2>&1
if %errorlevel% neq 0 (
    echo [i] Requesting Administrator rights...
    powershell -Command "Start-Process '%~s0' -Verb RunAs"
    exit /b 0
)

echo [*] Adding Gum to PATH...
set "TARGET=%APP_DIR%"
for /f "skip=2 tokens=1,2,*" %%a in ('reg query "HKCU\Environment" /v PATH 2^>nul') do set "USER_PATH=%%c"
if "%USER_PATH%"=="" set "USER_PATH="
echo "%USER_PATH%" | find /i "%TARGET%" >nul 2>&1
if %errorlevel% equ 0 (
    echo [v] Already in PATH
) else (
    if "%USER_PATH%"=="" (
        setx PATH "%TARGET%" >nul
    ) else (
        setx PATH "%USER_PATH%;%TARGET%" >nul
    )
    if !errorlevel! equ 0 echo [v] Added to PATH
)

echo [*] Creating file association...

for %%e in (gum sek tux) do (
    assoc .%%e=SekLang.Script >nul 2>&1
    reg add "HKCR\.%%e" /ve /d "SekLang.Script" /f >nul
    reg add "HKCU\Software\Classes\.%%e" /ve /d "SekLang.Script" /f >nul
    reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.%%e" /f >nul 2>&1
    reg delete "HKLM\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.%%e" /f >nul 2>&1
)

ftype SekLang.Script="%SEKC_EXE%" "%%1" %%* >nul 2>&1

reg add "HKCR\SekLang.Script" /ve /d "SekLang Script" /f >nul
reg add "HKCR\SekLang.Script\shell\open\command" /ve /d "\"%SEKC_EXE%\" \"%%1\" %%*" /f >nul
reg add "HKCR\SekLang.Script\DefaultIcon" /ve /d "\"%SEKC_EXE%\",0" /f >nul

reg add "HKCU\Software\Classes\SekLang.Script" /ve /d "SekLang Script" /f >nul
reg add "HKCU\Software\Classes\SekLang.Script\shell\open\command" /ve /d "\"%SEKC_EXE%\" \"%%1\" %%*" /f >nul
reg add "HKCU\Software\Classes\SekLang.Script\DefaultIcon" /ve /d "\"%SEKC_EXE%\",0" /f >nul

echo [v] .gum, .sek and .tux now open with sekc.exe

echo.
echo [*] Refreshing environment variables...
setx SEKLANG_INSTALL_DIR "%APP_DIR%" >nul

echo.
echo ============================================
echo   Installation complete!
echo ============================================
echo.
echo   sekc.exe      : %SEKC_EXE%
echo   .gum / .sek / .tux : double-click to run
echo   PATH          : %TARGET%
echo.
echo   If Windows still asks how to open:
echo     1. Right-click .sek file ^> Open with
echo     2. Choose "sekc.exe"
echo     3. Check "Always use this app"
echo     4. Click OK
echo.
echo   Restart terminal or log out for PATH.
echo.
pause
