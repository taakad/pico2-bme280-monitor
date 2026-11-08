@echo off
rem Pico2 BME280 Monitor セットアップ(Windows)
rem Node.js / PostgreSQL / TimescaleDB の導入とコレクターの常駐化を行う。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1" -Service
pause
