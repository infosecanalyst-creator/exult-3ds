@echo off
cd /d "%~dp0"
echo Exult 3DS: installing / updating over Wi-Fi (open ftpd on the 3DS first)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Deploy-Exult.ps1" -Install
pause
