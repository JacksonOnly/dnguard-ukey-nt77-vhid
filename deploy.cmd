@echo off
setlocal
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy.ps1" %*
set "rc=%ERRORLEVEL%"
if not "%rc%"=="0" (
  echo.
  echo Deployment failed with exit code %rc%.
  pause
)
exit /b %rc%
