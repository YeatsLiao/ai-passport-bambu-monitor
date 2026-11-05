@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\device\capture_screen.ps1" %*
if errorlevel 1 goto failed
exit /b 0
:failed
echo.
echo Screenshot failed. Read the message above. Check the selected port and device connection.
pause
exit /b 1
