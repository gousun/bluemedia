@echo off
rem Build BlueMedia standalone exe with PyInstaller, then zip for GitHub Release.
rem Usage: build_exe.bat 1.0.0
cd /d %~dp0

set VERSION=%1
if "%VERSION%"=="" set VERSION=1.0.0

python -m pip show pyinstaller >nul 2>nul || python -m pip install pyinstaller

python -m PyInstaller --noconfirm --clean --windowed --name BlueMedia ^
  --add-data "web;web" ^
  --hidden-import comtypes.stream ^
  media_gui.py
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)

powershell -NoProfile -Command "Compress-Archive -Path 'dist\BlueMedia\*' -DestinationPath 'dist\BlueMedia-v%VERSION%.zip' -Force"
echo.
echo DONE: dist\BlueMedia-v%VERSION%.zip
echo Publish: gh release create v%VERSION% "dist\BlueMedia-v%VERSION%.zip" --title "v%VERSION%" --notes "..."
