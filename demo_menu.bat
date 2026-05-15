@echo off
set "MOSQ=C:\Program Files\mosquitto\mosquitto_pub.exe"
set "HOST=localhost"
set "PORT=1883"
set "USER="
set "PASS="
if exist "dashboard.local.env.bat" call "dashboard.local.env.bat"
if defined MOSQUITTO_PUB_EXE set "MOSQ=%MOSQUITTO_PUB_EXE%"
if defined MQTT_HOST set "HOST=%MQTT_HOST%"
if defined MQTT_PORT set "PORT=%MQTT_PORT%"
if defined MQTT_USER set "USER=%MQTT_USER%"
if defined MQTT_PASS set "PASS=%MQTT_PASS%"
set "TOPIC=niang-lo-hanne-ndour/classroom/comfort/config/system_mode"

:menu
cls
title Smart Classroom Demo Menu
echo ==========================================
echo Smart Classroom Demo Control
echo ==========================================
echo.
echo 1. Real mode
echo 2. Empty and cool
echo 3. Occupied and comfortable
echo 4. Occupied and warm
echo 5. Occupied and hot
echo 6. Exit
echo.
choice /c 123456 /n /m "Choose a mode: "

if errorlevel 6 goto end
if errorlevel 5 goto hot
if errorlevel 4 goto warm
if errorlevel 3 goto comfy
if errorlevel 2 goto cool
if errorlevel 1 goto real

:real
call :publish "real"
echo.
echo Sent: real
timeout /t 2 >nul
goto menu

:cool
call :publish "demo_empty_cool"
echo.
echo Sent: demo_empty_cool
timeout /t 2 >nul
goto menu

:comfy
call :publish "demo_occupied_comfy"
echo.
echo Sent: demo_occupied_comfy
timeout /t 2 >nul
goto menu

:warm
call :publish "demo_occupied_warm"
echo.
echo Sent: demo_occupied_warm
timeout /t 2 >nul
goto menu

:hot
call :publish "demo_occupied_hot"
echo.
echo Sent: demo_occupied_hot
timeout /t 2 >nul
goto menu

:end
goto :eof

:publish
if defined USER (
  "%MOSQ%" -h "%HOST%" -p "%PORT%" -u "%USER%" -P "%PASS%" -t "%TOPIC%" -m %~1 -r
) else (
  "%MOSQ%" -h "%HOST%" -p "%PORT%" -t "%TOPIC%" -m %~1 -r
)
goto :eof
