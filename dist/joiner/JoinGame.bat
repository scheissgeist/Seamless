@echo off
setlocal enabledelayedexpansion
title DS2 Seamless Co-op
echo.
echo   DS2 SEAMLESS CO-OP
echo   ==================
echo.

:: Find Steam install path from registry
set "STEAM_PATH="
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM_PATH=%%b"
if not defined STEAM_PATH (
    for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Valve\Steam" /v InstallPath 2^>nul') do set "STEAM_PATH=%%b"
)

:: Search for DS2 in all Steam library folders
set "GAME_DIR="
set "DS2_FOLDER=steamapps\common\Dark Souls II Scholar of the First Sin\Game"

:: Check main Steam path
if defined STEAM_PATH (
    if exist "!STEAM_PATH!\!DS2_FOLDER!\DarkSoulsII.exe" (
        set "GAME_DIR=!STEAM_PATH!\!DS2_FOLDER!"
    )
)

:: Parse libraryfolders.vdf for additional Steam libraries
if not defined GAME_DIR if defined STEAM_PATH (
    if exist "!STEAM_PATH!\steamapps\libraryfolders.vdf" (
        for /f "tokens=*" %%L in ('findstr /C:"path" "!STEAM_PATH!\steamapps\libraryfolders.vdf" 2^>nul') do (
            for /f "tokens=2 delims=	" %%P in ("%%L") do (
                set "LIB_PATH=%%~P"
                set "LIB_PATH=!LIB_PATH:"=!"
                if exist "!LIB_PATH!\!DS2_FOLDER!\DarkSoulsII.exe" (
                    set "GAME_DIR=!LIB_PATH!\!DS2_FOLDER!"
                )
            )
        )
    )
)

:: Common fallback paths
if not defined GAME_DIR (
    for %%D in (
        "C:\Program Files (x86)\Steam\!DS2_FOLDER!"
        "C:\Program Files\Steam\!DS2_FOLDER!"
        "D:\Steam\!DS2_FOLDER!"
        "D:\SteamLibrary\!DS2_FOLDER!"
        "E:\Steam\!DS2_FOLDER!"
        "E:\SteamLibrary\!DS2_FOLDER!"
        "F:\Steam\!DS2_FOLDER!"
        "F:\SteamLibrary\!DS2_FOLDER!"
    ) do (
        if exist "%%~D\DarkSoulsII.exe" set "GAME_DIR=%%~D"
    )
)

:: Still not found — ask the user
if not defined GAME_DIR (
    echo   Could not find Dark Souls II automatically.
    echo.
    echo   Find your DarkSoulsII.exe file, then paste the folder path here.
    echo   Example: D:\SteamLibrary\steamapps\common\Dark Souls II Scholar of the First Sin\Game
    echo.
    set /p GAME_DIR="   DS2 Game folder: "
    echo.
    if not exist "!GAME_DIR!\DarkSoulsII.exe" (
        echo   DarkSoulsII.exe not found in that folder. Check the path and try again.
        pause
        exit /b 1
    )
)

echo   Found DS2: !GAME_DIR!
echo.

:: Check if already installed with a valid IP
if exist "!GAME_DIR!\ds2_seamless_coop.ini" (
    findstr /C:"CHANGE_ME" "!GAME_DIR!\ds2_seamless_coop.ini" >nul 2>&1
    if errorlevel 1 (
        echo   Mod already installed. Launching DS2...
        start steam://rungameid/335300
        timeout /t 3 >nul
        exit
    )
)

:: First time setup
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
    echo   ERROR: Could not copy files. Try running as Administrator.
    pause
    exit /b 1
)

echo   Mod installed! Server IP: !HOST_IP!
echo.
echo   Launching Dark Souls II...
start steam://rungameid/335300
echo.
echo   Press INSERT in-game for the co-op menu.
timeout /t 5 >nul
