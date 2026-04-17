@echo off
REM Deploy the joiner payload to Batcomputer's DS2 Game folder.
REM Uses the Marvel-method (net use Z:) over SMB.
REM
REM Requires Batcomputer to have the DS2 Game folder shared as
REM   \\Batcomputer\Dark Souls II Scholar of the First Sin
REM with user Batman / S0mbrer0 having write access.
REM
REM If DS2 is running on Batcomputer, dinput8.dll will be locked and
REM the copy will fail. Close DS2 on Batcomputer first.
REM
REM Output is tee'd to deploypc2_joiner_out.txt so you can inspect
REM what happened after the fact.

set "PC2_SHARE=\\BATCOMPUTER\Dark Souls II Scholar of the First Sin\Game"
set "PC2_USER=Batman"
set "PC2_PASS=S0mbrer0"
set "SRC=%~dp0dist\joiner"
set "LOG=%~dp0deploypc2_joiner_out.txt"

echo ================================================ > "%LOG%"
echo DS2 Seamless Co-op - Deploy to Batcomputer (JOINER) >> "%LOG%"
echo %DATE% %TIME% >> "%LOG%"
echo ================================================ >> "%LOG%"

REM --- Verify source files exist on THIS machine first ---
for %%F in (dinput8.dll ds2_seamless_coop.ini ds2_server_public.key JoinGame.bat) do (
    if not exist "%SRC%\%%F" (
        echo ERROR: Missing source file: %SRC%\%%F >> "%LOG%"
        type "%LOG%"
        exit /b 1
    )
)

REM --- Mount ---
net use Z: "%PC2_SHARE%" /user:%PC2_USER% %PC2_PASS% /persistent:no >> "%LOG%" 2>&1
echo net use result: %ERRORLEVEL% >> "%LOG%"
if %ERRORLEVEL% NEQ 0 (
    echo Mount failed. Is Batcomputer awake and sharing that folder? >> "%LOG%"
    type "%LOG%"
    exit /b 1
)

REM --- Copy the four joiner files ---
copy /Y "%SRC%\dinput8.dll"             "Z:\dinput8.dll"             >> "%LOG%" 2>&1
set "R1=%ERRORLEVEL%"
copy /Y "%SRC%\ds2_seamless_coop.ini"   "Z:\ds2_seamless_coop.ini"   >> "%LOG%" 2>&1
set "R2=%ERRORLEVEL%"
copy /Y "%SRC%\ds2_server_public.key"   "Z:\ds2_server_public.key"   >> "%LOG%" 2>&1
set "R3=%ERRORLEVEL%"
copy /Y "%SRC%\JoinGame.bat"            "Z:\JoinGame.bat"            >> "%LOG%" 2>&1
set "R4=%ERRORLEVEL%"

echo copy results: dll=%R1% ini=%R2% key=%R3% bat=%R4% >> "%LOG%"

REM --- Unmount ---
net use Z: /delete /y >> "%LOG%" 2>&1

type "%LOG%"

if "%R1%%R2%%R3%%R4%"=="0000" (
    echo.
    echo PC2 joiner deploy succeeded.
    exit /b 0
) else (
    echo.
    echo PC2 joiner deploy FAILED. Is DS2 running on Batcomputer?
    exit /b 1
)
