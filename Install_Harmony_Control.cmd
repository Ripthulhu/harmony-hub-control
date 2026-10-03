@echo off
setlocal EnableExtensions
cd /d "%~dp0"

where py >nul 2>nul
if %errorlevel%==0 (
  py -3 "%~dp0setup.py" %*
) else (
  python "%~dp0setup.py" %*
)

echo.
if errorlevel 1 (
  echo Install failed.
) else (
  echo Install finished.
)
echo.
pause
