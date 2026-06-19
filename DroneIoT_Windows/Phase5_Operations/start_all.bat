@echo off
REM ============================================================
REM start_all.bat — Windows Phase 5: Khởi động toàn bộ hệ thống
REM Tuân theo đúng thứ tự khởi động
REM ============================================================

SET ROOT_DIR=%~dp0..

echo ====================================================
echo   Drone IoT - Start All (Windows)
echo   Thu tu: Docker - BW16 - SITL - Fusion - QGC
echo ====================================================
echo.

REM Buoc 1: Docker
echo --- [1/4] Khoi dong Docker containers ---
docker info >NUL 2>&1
IF ERRORLEVEL 1 (
    echo [ERROR] Docker Desktop chua chay!
    echo         Mo Docker Desktop va cho bieu tuong TaskBar chuyen xanh.
    pause
    exit /b 1
)
cd /d "%ROOT_DIR%\Phase1_Docker"
docker-compose up -d
echo [OK] Docker: iot_mqtt, iot_db, iot_grafana
echo Cho 10 giay de InfluxDB init...
timeout /t 10 /nobreak >NUL

REM Buoc 2: BW16
echo.
echo --- [2/4] Kiem tra BW16 Board ---
echo   - Cap nguon board BW16 (USB hoac nguon ngoai)
echo   - Quan sat LED: nhay deu = WiFi OK
echo.
set /p bw16_ok="  BW16 da ket noi WiFi chua? [y/n]: "
IF /I NOT "%bw16_ok%"=="y" (
    echo   Tiep tuc - fusion.py se cho data tu BW16.
)

REM Buoc 3: SITL trong WSL2
echo.
echo --- [3/4] Khoi dong SITL trong WSL2 ---
echo   Mo cua so PowerShell MOI va chay:
echo   %ROOT_DIR%\Phase2_SITL\run_sitl.ps1
echo.
echo   Cho SITL hien "MAV^>" roi nhan Enter...
pause

REM Kiem tra port 5760
netstat -an | findstr ":5760" >NUL 2>&1
IF ERRORLEVEL 1 (
    echo [WARN] Port 5760 chua mo - SITL co the chua san sang.
    echo        Doi them 30 giay...
    timeout /t 30 /nobreak >NUL
) ELSE (
    echo [OK] Port 5760 dang mo.
)

REM Buoc 4: Fusion Gateway
echo.
echo --- [4/4] Khoi dong Data Fusion Gateway ---
SET VENV_DIR=%ROOT_DIR%\Phase4_Fusion\drone_env
IF NOT EXIST "%VENV_DIR%" (
    echo Chua co venv. Dang tao...
    call "%ROOT_DIR%\Phase4_Fusion\setup_venv.bat"
)

REM Khoi dong fusion.py trong cua so moi
echo Khoi dong fusion.py trong cua so moi...
start "Drone IoT Fusion" cmd /k "call "%VENV_DIR%\Scripts\activate.bat" && python "%ROOT_DIR%\Phase4_Fusion\fusion.py""
echo [OK] fusion.py dang chay (cua so moi)

echo.
echo ====================================================
echo   He thong da khoi dong!
echo.
echo   Grafana:   http://localhost:3000
echo   InfluxDB:  http://localhost:8086
echo   MQTT:      localhost:1883
echo.
echo   BUOC TIEP THEO:
echo   1. Mo QGroundControl - tu ket noi UDP 14550
echo   2. Mo http://localhost:3000 xem Grafana
echo ====================================================
pause
