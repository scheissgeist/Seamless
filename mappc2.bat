@echo off
REM Mount Batcomputer's DS2 Game folder as drive Z: via admin share c$.
REM
REM No Batcomputer-side share setup required IF the Batman account is a
REM local administrator on Batcomputer (admin shares c$, d$ etc. exist
REM automatically for admins).
REM
REM If this fails with error 5 (access denied), Batman isn't admin on
REM Batcomputer — either make it one, or fall back to creating an
REM explicit share for the DS2 Game folder.
REM
REM To unmount manually: net use Z: /delete /y

set "PC2_SHARE=\\BATCOMPUTER\Dark Souls II Scholar of the First Sin\Game"
set "PC2_USER=Batman"
set "PC2_PASS=S0mbrer0"

echo Mounting: %PC2_SHARE%
net use Z: "%PC2_SHARE%" /user:%PC2_USER% %PC2_PASS% /persistent:no
echo net use result: %ERRORLEVEL%

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Mount OK. DS2 Game folder contents:
    dir Z:\dinput8.dll Z:\DarkSoulsII.exe 2>nul
)
