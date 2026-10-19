@echo off
rem Doom-64-DC-X: one double-click from your ROM to Doom-64-DC-X.iso.
rem
rem   1. put your Doom 64 ROM (US v1.0 or v1.1) in this folder, named baserom.z64
rem   2. run this file (double-click, or "build.bat" in a console)
rem
rem It finds a Python 3.8 or later and hands over to setup.py, which checks
rem the ROM, fetches the Dreamcast port's data, extracts the game from the
rem ROM and builds. Arguments are passed on: build.bat --check,
rem build.bat --config Debug, build.bat --skip-build.
setlocal EnableExtensions
set "D64X_SELF=%~f0"
rem A Visual Studio build of an RXDK project runs its Pre-Build Event with no
rem PATH at all; put the Windows folders back so py and where resolve.
if not defined PATH set "PATH=%SystemRoot%\System32;%SystemRoot%;%SystemRoot%\System32\Wbem"
cd /d "%~dp0"
title Doom-64-DC-X build

echo.
echo  Doom-64-DC-X -- Doom 64 for the original Xbox
echo.

set "D64X_ROM="
for %%R in (baserom.z64 doom64.z64 baserom.v64 doom64.v64 baserom.n64 doom64.n64) do (
  if not defined D64X_ROM if exist "%%R" set "D64X_ROM=%%R"
)
if not defined D64X_ROM (
  echo  Your ROM is not here. Copy your Doom 64 ROM ^(US v1.0 or v1.1^) into
  echo      %~dp0
  echo  and name it  baserom.z64  then run this again.
  goto :fail
)

set "D64X_PY="
for %%V in (3.13 3.12 3.14 3.11 3.10 3.9 3.8) do (
  if not defined D64X_PY (
    py -%%V -c "import sys" >nul 2>nul && set "D64X_PY=py -%%V"
  )
)
if not defined D64X_PY (
  python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>nul && set "D64X_PY=python"
)
if not defined D64X_PY (
  echo  No Python 3.8 or later was found. Install it from
  echo      https://www.python.org/downloads/
  echo  ^(tick "Add python.exe to PATH"^), then run this again.
  goto :fail
)

echo  Python: %D64X_PY%
echo.
%D64X_PY% setup.py %*
set "D64X_RC=%ERRORLEVEL%"
if not "%D64X_RC%"=="0" goto :failrc
echo.
echo  Done.
call :pause
exit /b 0

:failrc
echo.
echo  The build stopped (exit code %D64X_RC%). The messages above say why.
call :pause
exit /b %D64X_RC%

:fail
echo.
call :pause
exit /b 1

rem ---- keep a double-clicked window open --------------------------------
:pause
if defined D64X_NONINTERACTIVE exit /b 0
echo %cmdcmdline% | find /i "%D64X_SELF%" >nul
if not errorlevel 1 pause
exit /b 0
