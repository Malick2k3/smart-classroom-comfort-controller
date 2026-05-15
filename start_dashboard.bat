@echo off
title Smart Classroom Dashboard
cd /d "C:\Users\User\OneDrive\Documents\Senior 1\Iot and Embeded systems\project"
if exist "dashboard.local.env.bat" call "dashboard.local.env.bat"
echo Starting dashboard from:
echo %CD%
echo.
python dashboard_app.py
echo.
echo Dashboard stopped or failed to start.
pause
