@echo off
REM ============================================================
REM setup.bat — Windows Phase 1: Dựng Docker server trung tâm
REM Chạy 1 lần duy nhất để khởi tạo toàn bộ infrastructure
REM Yêu cầu: Docker Desktop với WSL2 backend đang chạy
REM ============================================================

echo ========================================
echo   Drone IoT - Phase 1 Docker Setup
echo   Platform: Windows (Docker Desktop + WSL2)
echo ========================================

REM Kiểm tra Docker đang chạy
docker info >NUL 2>&1
IF ERRORLEVEL 1 (
    echo [ERROR] Docker Desktop chua chay.
    echo         Mo Docker Desktop, cho bieu tuong Taskbar chuyen XANH.
    pause
    exit /b 1
)
echo [OK] Docker Engine dang chay.

REM Chuyển đến thư mục chứa docker-compose.yml
cd /d "%~dp0"

echo.
echo [*] Khoi dong containers...
docker-compose up -d
IF ERRORLEVEL 1 (
    echo [ERROR] docker-compose up that bai.
    pause
    exit /b 1
)

echo.
echo [*] Cho InfluxDB khoi dong (15 giay)...
timeout /t 15 /nobreak >NUL

echo.
echo [*] Kiem tra containers:
docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

echo.
echo [*] Cho them 5 giay de InfluxDB init xong...
timeout /t 5 /nobreak >NUL

echo.
echo [*] Lay InfluxDB Token:
docker exec iot_db influx auth list --user admin --hide-headers 2>NUL | for /f "tokens=4" %%a in ('findstr /v "^$"') do (
    echo.
    echo ============================================================
    echo   TOKEN: %%a
    echo   Dan vao fusion.py o truong INFLUX_TOKEN
    echo ============================================================
    echo %%a > "%~dp0..\Phase4_Fusion\.influx_token"
    goto :token_done
)
:token_done

echo.
echo [*] Test MQTT broker...
REM Publish test message
docker exec iot_mqtt mosquitto_pub -t "drone/test" -m "hello_drone_iot_windows"
echo [OK] MQTT Publish thanh cong (kiem tra thu cong neu can).

echo.
echo ========================================
echo   Phase 1 hoan tat!
echo   Grafana:  http://localhost:3000
echo   InfluxDB: http://localhost:8086
echo   MQTT:     localhost:1883
echo ========================================
pause
