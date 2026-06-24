@echo off
REM ============================================================
REM setup_venv.bat — Windows Phase 4: Tạo môi trường Python ảo
REM Yêu cầu: Python 3.8+ đã cài trên Windows
REM ============================================================

echo ========================================
echo   Drone IoT - Phase 4 Python venv Setup
echo   Platform: Windows
echo ========================================

REM Kiem tra Python
python --version >NUL 2>&1
IF ERRORLEVEL 1 (
    echo [ERROR] Python chua cai. Tai tai: https://python.org
    echo         Bật tùy chọn "Add Python to PATH" khi cài!
    pause
    exit /b 1
)

echo [OK] Python da co:
python --version

REM Tao venv
SET VENV_DIR=%~dp0drone_env

IF EXIST "%VENV_DIR%" (
    echo [OK] venv da ton tai: %VENV_DIR%
) ELSE (
    echo [*] Tao virtual environment...
    python -m venv "%VENV_DIR%"
    echo [OK] Da tao venv.
)

REM Kich hoat va cai thu vien
echo.
echo [*] Cai dependencies...
call "%VENV_DIR%\Scripts\activate.bat"
python -m pip install --upgrade pip --quiet
pip install -r "%~dp0requirements.txt"

echo.
echo [*] Verify:
python -c "from pymavlink import mavutil; print('  [OK] pymavlink')"
python -c "import paho.mqtt.client as mqtt; print('  [OK] paho-mqtt')"
python -c "from influxdb_client import InfluxDBClient; print('  [OK] influxdb-client')"

echo.
echo ========================================
echo   Phase 4 Setup hoan tat!
echo.
echo   De chay fusion.py:
echo   %VENV_DIR%\Scripts\activate
echo   python %~dp0fusion.py
echo ========================================
pause
