@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-sbgl-bridge.ps1" %*
if errorlevel 1 (
  echo.
  echo SBGL Bridge installation failed. Read the error above.
  pause
  exit /b 1
)
echo.
pause
