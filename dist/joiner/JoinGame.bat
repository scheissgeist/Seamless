@echo off
title DS2 Seamless Co-op
echo.
echo   DS2 SEAMLESS CO-OP
echo   ==================
echo.

:: Try auto-detect first
set "GAME_DIR="
set "DS2=steamapps\common\Dark Souls II Scholar of the First Sin\Game"

:: Check common locations
if exist "C:\Program Files (x86)\Steam\%DS2%\DarkSoulsII.exe" set "GAME_DIR=C:\Program Files (x86)\Steam\%DS2%"
if exist "C:\Program Files\Steam\%DS2%\DarkSoulsII.exe" set "GAME_DIR=C:\Program Files\Steam\%DS2%"
if exist "D:\Steam\%DS2%\DarkSoulsII.exe" set "GAME_DIR=D:\Steam\%DS2%"
if exist "D:\SteamLibrary\%DS2%\DarkSoulsII.exe" set "GAME_DIR=D:\SteamLibrary\%DS2%"
if exist "E:\Steam\%DS2%\DarkSoulsII.exe" set "GAME_DIR=E:\Steam\%DS2%"
if exist "E:\SteamLibrary\%DS2%\DarkSoulsII.exe" set "GAME_DIR=E:\SteamLibrary\%DS2%"
if exist "F:\Steam\%DS2%\DarkSoulsII.exe" set "GAME_DIR=F:\Steam\%DS2%"
if exist "F:\SteamLibrary\%DS2%\DarkSoulsII.exe" set "GAME_DIR=F:\SteamLibrary\%DS2%"
if exist "G:\Steam\%DS2%\DarkSoulsII.exe" set "GAME_DIR=G:\Steam\%DS2%"
if exist "G:\SteamLibrary\%DS2%\DarkSoulsII.exe" set "GAME_DIR=G:\SteamLibrary\%DS2%"

if defined GAME_DIR (
    echo   Found DS2 at: %GAME_DIR%
    echo.
    echo   [1] Use this path
    echo   [2] Enter a different path
    echo.
    set /p CHOICE="   Choice (1/2): "
    if "%CHOICE%"=="2" set "GAME_DIR="
)

if not defined GAME_DIR (
    echo   Could not auto-detect DS2.
    echo.
    echo   Right-click DS2 in Steam, Manage, Browse Local Files
    echo   then copy and paste that path here.
    echo.
    set /p GAME_DIR="   DS2 Game folder: "
    echo.
)

if not exist "%GAME_DIR%\DarkSoulsII.exe" (
    echo   ERROR: DarkSoulsII.exe not found at that path.
    pause
    exit /b 1
)

:: Check if already configured
if exist "%GAME_DIR%\ds2_seamless_coop.ini" (
    echo   Mod already installed.
    echo.
    echo   [1] Launch game
    echo   [2] Change host IP
    echo.
    set /p ACTION="   Choice (1/2): "
    if "%ACTION%"=="1" (
        echo   Launching DS2...
        start steam://rungameid/335300
        timeout /t 3 >nul
        exit
    )
)

:: Ask for host IP
echo.
echo   Enter the HOST's Hamachi IP address
echo   (ask your friend for their 25.x.x.x IP)
echo.
set /p HOST_IP="   Host IP: "

if "%HOST_IP%"=="" (
    echo   No IP entered.
    pause
    exit /b 1
)

:: Write config
echo enabled=true> "%GAME_DIR%\ds2_seamless_coop.ini"
echo debug_logging=true>> "%GAME_DIR%\ds2_seamless_coop.ini"
echo max_players=4>> "%GAME_DIR%\ds2_seamless_coop.ini"
echo port=27015>> "%GAME_DIR%\ds2_seamless_coop.ini"
echo use_custom_server=true>> "%GAME_DIR%\ds2_seamless_coop.ini"
echo server_ip=%HOST_IP%>> "%GAME_DIR%\ds2_seamless_coop.ini"
echo server_port=50031>> "%GAME_DIR%\ds2_seamless_coop.ini"
echo allow_invasions=false>> "%GAME_DIR%\ds2_seamless_coop.ini"

:: Copy mod files
copy /y "%~dp0dinput8.dll" "%GAME_DIR%\" >nul 2>&1
copy /y "%~dp0ds2_server_public.key" "%GAME_DIR%\" >nul 2>&1

echo.
echo   Installed! Server IP: %HOST_IP%
echo   Launching DS2...
start steam://rungameid/335300
echo.
echo   Press INSERT in-game for the co-op menu.
timeout /t 5 >nul
