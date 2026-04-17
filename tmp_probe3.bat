@echo off
set "PC2_SHARE=\\BATCOMPUTER\Dark Souls II Scholar of the First Sin\Game"
set "PC2_USER=Batman"
set "PC2_PASS=S0mbrer0"

net use Z: "%PC2_SHARE%" /user:%PC2_USER% %PC2_PASS% /persistent:no
echo mount: %ERRORLEVEL%

echo.
echo --- icacls on the shared root (Z:\) ---
icacls Z:\

echo.
echo --- icacls on the DarkSoulsII.exe file (known to exist) ---
icacls "Z:\DarkSoulsII.exe"

echo.
echo --- SMB share access rights (net share info is client-side blind, so try write) ---

echo test > Z:\_write_test.txt
echo write errorlevel: %ERRORLEVEL%
if exist Z:\_write_test.txt (
    echo file_created: YES
    del Z:\_write_test.txt
) else (
    echo file_created: NO
)

net use Z: /delete /y
