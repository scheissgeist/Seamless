@echo off
title DS2 Seamless Co-op
echo.
echo   DS2 SEAMLESS CO-OP
echo   ==================
echo.

:: Check if already configured
set GAME_DIR=
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do set STEAM_PATH=%%b
if defined STEAM_PATH (
    set "GAME_DIR=%STEAM_PATH%\steamapps\common\Dark Souls II Scholar of the First Sin\Game"
)
if not defined GAME_DIR (
    set "GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\Dark Souls II Scholar of the First Sin\Game"
)

:: Check if already installed with a valid IP
if exist "%GAME_DIR%\ds2_seamless_coop.ini" (
    findstr /C:"server_ip=CHANGE_ME" "%GAME_DIR%\ds2_seamless_coop.ini" >nul 2>&1
    if errorlevel 1 (
        echo   Mod already installed. Launching DS2...
        echo.
        start steam://rungameid/335300
        exit
    )
)

:: First time setup
echo   Enter the HOST's Hamachi IP address
echo   (ask your friend for their 25.x.x.x IP)
echo.
set /p HOST_IP="   Host IP: "
echo.

:: Write the INI
(
echo enabled=true
echo debug_logging=false
echo max_players=4
echo port=27015
echo use_custom_server=true
echo server_ip=%HOST_IP%
echo server_port=50031
echo allow_invasions=false
echo sync_bonfires=true
echo sync_items=false
echo sync_enemies=false
) > "%~dp0ds2_seamless_coop.ini"

if exist "%GAME_DIR%\DarkSoulsII.exe" (
    copy /y "%~dp0dinput8.dll" "%GAME_DIR%\" >nul
    copy /y "%~dp0ds2_seamless_coop.ini" "%GAME_DIR%\" >nul
    copy /y "%~dp0ds2_server_public.key" "%GAME_DIR%\" >nul
    echo   Installed! Launching DS2...
    echo.
    start steam://rungameid/335300
) else (
    echo   Could not find DS2 automatically.
    echo   Copy these files to your DS2 Game folder:
    echo     dinput8.dll / ds2_seamless_coop.ini / ds2_server_public.key
    echo.
    pause
)
