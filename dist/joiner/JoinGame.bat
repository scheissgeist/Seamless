@echo off
setlocal enabledelayedexpansion
title DS2 Seamless Co-op
echo.
echo   DS2 SEAMLESS CO-OP
echo   ==================
echo.

:: Find Steam install path
set "STEAM_PATH="
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM_PATH=%%b"
if not defined STEAM_PATH (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM_PATH=%%b"
)

:: Build game path
set "GAME_DIR="
if defined STEAM_PATH (
    if exist "!STEAM_PATH!\steamapps\common\Dark Souls II Scholar of the First Sin\Game\DarkSoulsII.exe" (
        set "GAME_DIR=!STEAM_PATH!\steamapps\common\Dark Souls II Scholar of the First Sin\Game"
    )
)
:: Fallback paths
if not defined GAME_DIR (
    if exist "C:\Program Files (x86)\Steam\steamapps\common\Dark Souls II Scholar of the First Sin\Game\DarkSoulsII.exe" (
        set "GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\Dark Souls II Scholar of the First Sin\Game"
    )
)
if not defined GAME_DIR (
    if exist "D:\SteamLibrary\steamapps\common\Dark Souls II Scholar of the First Sin\Game\DarkSoulsII.exe" (
        set "GAME_DIR=D:\SteamLibrary\steamapps\common\Dark Souls II Scholar of the First Sin\Game"
    )
)

if not defined GAME_DIR (
    echo   Could not find Dark Souls II automatically.
    echo.
    set /p GAME_DIR="   Enter your DS2 Game folder path: "
    echo.
)

:: Check if already installed with a valid IP
if exist "!GAME_DIR!\ds2_seamless_coop.ini" (
    findstr /C:"CHANGE_ME" "!GAME_DIR!\ds2_seamless_coop.ini" >nul 2>&1
    if errorlevel 1 (
        echo   Mod already installed. Launching DS2...
        echo.
        start steam://rungameid/335300
        timeout /t 3 >nul
        exit
    )
)

:: First time setup — ask for host IP
echo   Enter the HOST's Hamachi IP address
echo   (ask your friend for their 25.x.x.x IP)
echo.
set /p HOST_IP="   Host IP: "
echo.

if "!HOST_IP!"=="" (
    echo   No IP entered. Exiting.
    pause
    exit /b 1
)

:: Write the INI
(
echo enabled=true
echo debug_logging=true
echo max_players=4
echo port=27015
echo use_custom_server=true
echo server_ip=!HOST_IP!
echo server_port=50031
echo allow_invasions=false
echo sync_bonfires=true
echo sync_items=false
echo sync_enemies=false
) > "!GAME_DIR!\ds2_seamless_coop.ini"

:: Copy mod files
copy /y "%~dp0dinput8.dll" "!GAME_DIR!\" >nul 2>&1
copy /y "%~dp0ds2_server_public.key" "!GAME_DIR!\" >nul 2>&1

if errorlevel 1 (
    echo   ERROR: Could not copy files. Run as Administrator?
    echo.
    pause
    exit /b 1
)

echo   Mod installed to: !GAME_DIR!
echo   Server IP: !HOST_IP!
echo.
echo   Launching Dark Souls II...
start steam://rungameid/335300
echo.
echo   Press INSERT in-game for the co-op menu.
echo.
timeout /t 5 >nul
