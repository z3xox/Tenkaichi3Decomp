@echo off
REM Build a drop-in patcheu\ folder from a Europe disc (SLES-549.45).
REM Usage:
REM   make_patcheu.bat
REM   make_patcheu.bat "C:\path\Europe.iso"
REM   (or drag the Europe .iso onto this .bat)

setlocal
cd /d "%~dp0"

set "ISO=%~1"
if "%ISO%"=="" (
  echo.
  echo  Tenkaichi3Decomp - create patcheu from Europe disc
  echo.
  echo  Drag your Europe ISO here, or paste the path and press Enter:
  set /p "ISO=  ISO: "
)
if "%ISO%"=="" (
  echo No ISO given.
  exit /b 1
)

where python >nul 2>&1
if errorlevel 1 (
  where py >nul 2>&1
  if errorlevel 1 (
    echo Python 3 is required. Install it from https://www.python.org/ and try again.
    exit /b 1
  )
  py -3 port\tools\make_patcheu.py --eu-iso "%ISO%" --out "%~dp0patcheu"
) else (
  python port\tools\make_patcheu.py --eu-iso "%ISO%" --out "%~dp0patcheu"
)

if errorlevel 1 (
  echo Failed.
  exit /b 1
)

echo.
echo Done. Folder: %~dp0patcheu
echo Put patcheu next to Tenkaichi3Decomp.exe (same folder as gamedata).
echo In game: F1 - Menu and dialogue text - Italian
pause
endlocal
