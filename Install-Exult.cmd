@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright (C) 2026 the exult-3ds authors
cd /d "%~dp0"
echo Exult 3DS: installing / updating over Wi-Fi (open ftpd on the 3DS first)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Deploy-Exult.ps1" -Install
pause
