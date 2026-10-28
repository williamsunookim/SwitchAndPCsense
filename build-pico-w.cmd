@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" -Board pico_w %*
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
echo Build complete.
pause
