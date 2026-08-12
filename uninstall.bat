@echo off
setlocal enabledelayedexpansion

title Gum Uninstaller

set "APP_DIR=%~dp0"
set "APP_DIR=%APP_DIR:~0,-1%"

fltmc >nul 2>&1
if %errorlevel% neq 0 (
    powershell "Start-Process '%~s0' -Verb RunAs"
    exit /b 0
)

for %%e in (gum sek tux) do (
    reg delete "HKCR\.%%e" /f >nul 2>&1
    reg delete "HKCU\Software\Classes\.%%e" /f >nul 2>&1
    reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.%%e" /f >nul 2>&1
)
reg delete "HKCR\SekLang.Script" /f >nul 2>&1
reg delete "HKCU\Software\Classes\SekLang.Script" /f >nul 2>&1
echo [v] .gum, .sek and .tux associations removed

echo [*] Removing Gum from PATH...
set "TARGET=%APP_DIR%"
set "USER_PATH="
for /f "skip=2 tokens=1,2,*" %%a in ('reg query "HKCU\Environment" /v PATH 2^>nul') do set "USER_PATH=%%c"
if not "%USER_PATH%"=="" (
    set "NEW_PATH="
    for %%p in ("%USER_PATH:;=" "%") do (
        if /i "%%~p" neq "%TARGET%" (
            if defined NEW_PATH (
                set "NEW_PATH=!NEW_PATH!;%%~p"
            ) else (
                set "NEW_PATH=%%~p"
            )
        )
    )
    if defined NEW_PATH (
        setx PATH "!NEW_PATH!" >nul
    ) else (
        reg delete "HKCU\Environment" /v PATH /f >nul 2>&1
    )
    echo [v] Removed from PATH
)

reg delete "HKCU\Environment" /v SEKLANG_INSTALL_DIR /f >nul 2>&1

echo.
echo Uninstall complete.
pause
