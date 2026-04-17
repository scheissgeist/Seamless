@echo off
set "PC2_SHARE=\\BATCOMPUTER\Dark Souls II Scholar of the First Sin\Game"
set "PC2_USER=Batman"
set "PC2_PASS=S0mbrer0"

net use Z: "%PC2_SHARE%" /user:%PC2_USER% %PC2_PASS% /persistent:no
echo mount: %ERRORLEVEL%

echo test > Z:\_write_test.txt
echo write_probe: %ERRORLEVEL%

dir Z:\_write_test.txt 2>nul
echo list: %ERRORLEVEL%

del Z:\_write_test.txt 2>nul
echo delete: %ERRORLEVEL%

net use Z: /delete /y
