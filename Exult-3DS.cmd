@echo off
rem SPDX-License-Identifier: GPL-2.0-or-later
rem Copyright (C) 2026 the exult-3ds authors
title Exult for New 3DS
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Deploy-Exult.ps1" %*
echo.
pause
