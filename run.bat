@echo off
setlocal
chcp 65001 >nul

if "%~1"=="" (
  echo Usage: run.bat file.gum
  exit /b 1
)

call build.bat
if errorlevel 1 exit /b %errorlevel%

sekc.exe "%~1"
