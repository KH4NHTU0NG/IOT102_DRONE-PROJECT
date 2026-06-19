@echo off
REM ============================================================
REM stop_all.bat — Windows Phase 5: Dừng toàn bộ hệ thống
REM ============================================================

SET ROOT_DIR=%~dp0..

echo --- Drone IoT - Dung toan bo he thong ---
echo.

REM Dung fusion.py
echo [*] Dung fusion.py...
taskkill /F /FI "WINDOWTITLE eq Drone IoT Fusion" >NUL 2>&1
taskkill /F /IM python.exe /FI "WINDOWTITLE eq Drone IoT*" >NUL 2>&1
echo [OK] Fusion da dung (neu dang chay).

REM Dung SITL trong WSL2
echo [*] Dung SITL trong WSL2...
wsl pkill -f sim_vehicle.py 2>NUL
echo [OK] SITL da dung.

REM Dung Docker
echo [*] Dung Docker containers...
cd /d "%ROOT_DIR%\Phase1_Docker"
docker-compose down
echo [OK] Docker containers da dung.

echo.
echo --- He thong da dung hoan toan ---
pause
