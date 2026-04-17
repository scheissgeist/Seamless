@echo off
set "PC2_SHARE=\\BATCOMPUTER\Dark Souls II Scholar of the First Sin\Game"
set "PC2_USER=Batman"
set "PC2_PASS=S0mbrer0"

net use Z: "%PC2_SHARE%" /user:%PC2_USER% %PC2_PASS% /persistent:no
echo mount: %ERRORLEVEL%

echo.
echo --- Who am I on Batcomputer? ---
net use

echo.
echo --- Effective permissions on Z:\ ---
icacls Z:\

echo.
echo --- Try to write _write_test.txt ---
echo hello > Z:\_write_test.txt 2> Z_err.txt
type Z_err.txt 2>nul
del Z_err.txt 2>nul

echo.
echo --- Does _write_test.txt exist on Z:? ---
if exist Z:\_write_test.txt (
    echo YES
    del Z:\_write_test.txt 2>nul
) else (
    echo NO
)

net use Z: /delete /y
