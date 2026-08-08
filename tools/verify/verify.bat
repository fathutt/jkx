@echo off
REM Double-click me. Finds the Steam install of Jedi Academy on its own, runs
REM the measurements, and opens the report. Any arguments are passed through,
REM so "verify.bat --title jk2" works.

setlocal
set HERE=%~dp0

where py >nul 2>nul
if %errorlevel%==0 (
    py -3 "%HERE%verify.py" %*
    goto done
)

where python >nul 2>nul
if %errorlevel%==0 (
    python "%HERE%verify.py" %*
    goto done
)

echo Python 3 was not found on PATH.
echo Install it from https://www.python.org/downloads/ and run this again.
pause
exit /b 1

:done
set CODE=%errorlevel%
if exist "%HERE%verification-report.md" start "" "%HERE%verification-report.md"
echo.
pause
exit /b %CODE%
