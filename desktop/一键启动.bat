@echo off
rem BlueMedia one-click launcher (GUI)
cd /d %~dp0

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python not found in PATH. Please install Python 3.9+ first.
  pause
  exit /b 1
)

python -c "import bleak,pynput,webview" >nul 2>nul
if errorlevel 1 (
  echo First run: installing dependencies, please wait...
  python -m pip install -r requirements.txt
)

start "" pythonw media_gui.py
exit /b 0
