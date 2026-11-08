@echo off
rem Windows equivalent of scripts/collector.sh: start PostgreSQL, then the
rem collector in this console window (Ctrl-C stops it).
setlocal
set REPO=%~dp0..
set PGROOT=%LOCALAPPDATA%\pico2-bme280\pgsql
set PGDATA=%PGROOT%\data
set PGHOST=127.0.0.1
set PGPORT=5433
set PGDATABASE=pico_monitor
set PGUSER=pico

if exist "%PGROOT%\bin\pg_isready.exe" (
    "%PGROOT%\bin\pg_isready.exe" -h %PGHOST% -p %PGPORT% -q >nul 2>&1
    if errorlevel 1 "%PGROOT%\bin\pg_ctl.exe" -D "%PGDATA%" -l "%PGROOT%\server.log" -o "-p %PGPORT%" -w start
)

if not exist "%REPO%\ui\dist\ui\browser\index.html" (
    echo building the UI ^(first run^)...
    pushd "%REPO%\ui" && call npm install --no-audit --no-fund && call npm run build && popd
)

echo collector on http://127.0.0.1:8787
pushd "%REPO%\server"
call npm install --no-audit --no-fund >nul
node src\index.js
popd
