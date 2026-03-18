@echo off
echo ================================================
echo   DS2 Seamless Co-op - Custom Server
echo ================================================
echo.
echo Starting DS2 custom server...
echo.
echo Players connect their game to YOUR IP address.
echo Set server_ip in ds2_seamless_coop.ini to your IP.
echo.
echo Press Ctrl+C to stop the server.
echo ================================================
echo.

cd /d "%~dp0Server"
Server.exe
