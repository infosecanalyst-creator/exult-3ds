@echo off
title Exult for New 3DS
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Deploy-Exult.ps1" %*
echo.
pause
