@echo off
title DS2 Seamless Co-op — Join Game
echo.
echo   DS2 SEAMLESS CO-OP — JOINER
echo   ===========================
echo.
echo   Enter the HOST's Hamachi IP address
echo   (ask your friend for their 25.x.x.x IP)
echo.
set /p HOST_IP="   Host IP: "
echo.

:: Write the INI
(
echo # DS2 Seamless Co-op — Auto-configured by JoinGame.bat
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

echo   Config saved! server_ip=%HOST_IP%
echo.

:: Copy files to game folder
set GAME_DIR=
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\WOW6432Node\Valve\Steam" /v InstallPath 2^>nul') do set STEAM_PATH=%%b
if defined STEAM_PATH (
    set GAME_DIR=%STEAM_PATH%\steamapps\common\Dark Souls II Scholar of the First Sin\Game
)
if not defined GAME_DIR (
    set GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\Dark Souls II Scholar of the First Sin\Game
)

if exist "%GAME_DIR%\DarkSoulsII.exe" (
    echo   Found DS2 at: %GAME_DIR%
    copy /y "%~dp0dinput8.dll" "%GAME_DIR%\" >nul
    copy /y "%~dp0ds2_seamless_coop.ini" "%GAME_DIR%\" >nul
    copy /y "%~dp0ds2_server_public.key" "%GAME_DIR%\" >nul
    echo   Mod files installed!
    echo.
    echo   Launch Dark Souls II through Steam.
    echo   Press INSERT in-game to open the co-op menu.
) else (
    echo   Could not find DS2 automatically.
    echo   Copy these files manually to your DS2 Game folder:
    echo     - dinput8.dll
    echo     - ds2_seamless_coop.ini
    echo     - ds2_server_public.key
)

echo.
pause
