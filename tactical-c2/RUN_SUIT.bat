@echo off
rem Real suit: NU40 gateway (USB) + phone GPS on the same outer, one server on https :8443.
rem Edit the soldier IDs below to match the sensor boards (SOLDIER_ID), e.g. -Soldiers 1,2
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_gps.ps1" -Source serial -Soldiers 1
pause
