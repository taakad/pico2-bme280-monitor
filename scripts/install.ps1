# One-shot installer for Windows 10/11 (x64): Node.js, PostgreSQL 17,
# TimescaleDB, project dependencies, database schema. Everything lands under
# %LOCALAPPDATA%\pico2-bme280 - no admin rights, no system-wide changes.
#
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Service
#     (-Service registers a logon task that starts the collector, so plugging
#      in the Pico begins measurement immediately.)
#
# Re-running is safe: every step is skipped when already satisfied.
param(
    [switch]$Service
)

$ErrorActionPreference = 'Stop'

$NodeVersion  = '24.22.2'   # LTS line; Angular CLI needs >= 24.15
$PgZip        = 'postgresql-17.5-1-windows-x64-binaries.zip'
$TsdbVersion  = '2.31.0'

$repoRoot = Split-Path -Parent $PSScriptRoot
$base     = Join-Path $env:LOCALAPPDATA 'pico2-bme280'
$pgRoot   = Join-Path $base 'pgsql'
$pgData   = Join-Path $pgRoot 'data'
$cache    = Join-Path $base 'cache'
$pgPort   = if ($env:PGPORT) { $env:PGPORT } else { 5433 }

function Step($msg) { Write-Host "`n== $msg ==" -ForegroundColor Cyan }

function Fetch($url, $dest) {
    if (Test-Path $dest) { return }
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    Write-Host "downloading $url"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    for ($try = 1; $try -le 3; $try++) {
        try {
            Invoke-WebRequest -Uri $url -OutFile $dest -UseBasicParsing -TimeoutSec 600
            return
        } catch {
            Write-Warning "download failed (attempt $try): $($_.Exception.Message)"
            Start-Sleep -Seconds (5 * $try)
        }
    }
    throw "could not download: $url"
}

# --------------------------------------------------------------- Node.js ---
Step 'Node.js'
$nodeDir  = Join-Path $base "node\node-v$NodeVersion-win-x64"
$nodeExe  = Join-Path $nodeDir 'node.exe'
$needNode = $true
$found = Get-Command node -ErrorAction SilentlyContinue
if ($found) {
    $ver = ((& node -v).TrimStart('v'))
    $parts = $ver.Split('.')
    if ([int]$parts[0] -gt 24 -or ([int]$parts[0] -eq 24 -and [int]$parts[1] -ge 15)) {
        $needNode = $false
        Write-Host "found v$ver on PATH"
    } else {
        Write-Host "found v$ver but the Angular CLI needs >= v24.15"
    }
}
if ($needNode) {
    if (-not (Test-Path $nodeExe)) {
        $zip = Join-Path $cache "node-v$NodeVersion-win-x64.zip"
        Fetch "https://nodejs.org/dist/v$NodeVersion/node-v$NodeVersion-win-x64.zip" $zip
        Expand-Archive -Path $zip -DestinationPath (Join-Path $base 'node') -Force
    }
    # Prepend for this session and persist for the logon task.
    $env:Path = "$nodeDir;$env:Path"
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($userPath -notlike "*$nodeDir*") {
        [Environment]::SetEnvironmentVariable('Path', "$nodeDir;$userPath", 'User')
    }
    Write-Host "using $(& $nodeExe -v) from $nodeDir"
}

# ----------------------------------------------------------- PostgreSQL ----
Step 'PostgreSQL 17'
$pgBin = Join-Path $pgRoot 'bin'
if (Test-Path (Join-Path $pgBin 'pg_ctl.exe')) {
    Write-Host "already installed at $pgRoot"
} else {
    $zip = Join-Path $cache $PgZip
    Fetch "https://get.enterprisedb.com/postgresql/$PgZip" $zip
    Write-Host "extracting to $pgRoot ..."
    New-Item -ItemType Directory -Force -Path $base | Out-Null
    Expand-Archive -Path $zip -DestinationPath $base -Force
    # The zip contains a top-level 'pgsql' folder; move it into place.
    $extracted = Join-Path $base 'pgsql'
    if ($extracted -ne $pgRoot) {
        if (Test-Path $pgRoot) { Remove-Item -Recurse -Force $pgRoot }
        Move-Item $extracted $pgRoot
    }
}

# ------------------------------------------------------------- TimescaleDB -
Step "TimescaleDB $TsdbVersion"
$tsdbDll = Join-Path $pgRoot 'lib\timescaledb.dll'
if (Test-Path $tsdbDll) {
    Write-Host 'already installed'
} else {
    $zip = Join-Path $cache "timescaledb-postgresql-17-windows-amd64.zip"
    Fetch "https://github.com/timescale/timescaledb/releases/download/$TsdbVersion/timescaledb-postgresql-17-windows-amd64.zip" $zip
    $tmp = Join-Path $cache 'tsdb-extract'
    Expand-Archive -Path $zip -DestinationPath $tmp -Force
    $src = Join-Path $tmp 'timescaledb'
    Write-Host 'copying extension files into the PostgreSQL install ...'
    Copy-Item (Join-Path $src 'timescaledb*.dll') (Join-Path $pgRoot 'lib') -Force
    Copy-Item (Join-Path $src 'timescaledb.control') (Join-Path $pgRoot 'share\extension') -Force
    Copy-Item (Join-Path $src 'timescaledb--*.sql') (Join-Path $pgRoot 'share\extension') -Force
    Remove-Item -Recurse -Force $tmp
}

# ------------------------------------------ cluster, config, database ------
Step 'Database cluster + schema'
$pgCtl  = Join-Path $pgBin 'pg_ctl.exe'
$psql   = Join-Path $pgBin 'psql.exe'
$initdb = Join-Path $pgBin 'initdb.exe'

if (-not (Test-Path (Join-Path $pgData 'PG_VERSION'))) {
    Write-Host "initialising cluster at $pgData (superuser: pico, trust auth)"
    & $initdb -D $pgData -U pico --auth=trust | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'initdb failed' }
}

# TimescaleDB must be preloaded before CREATE EXTENSION can run.
$conf = Join-Path $pgData 'postgresql.conf'
$confText = Get-Content $conf -Raw
if ($confText -notmatch "(?m)^shared_preload_libraries = 'timescaledb'") {
    $confText = $confText -replace "(?m)^#?shared_preload_libraries = .*", "shared_preload_libraries = 'timescaledb'"
    if ($confText -notmatch "(?m)^shared_preload_libraries = 'timescaledb'") {
        $confText += "`nshared_preload_libraries = 'timescaledb'`n"
    }
    # ASCII: a UTF-8 BOM would make PostgreSQL reject the first conf line.
    Set-Content -Path $conf -Value $confText -Encoding ASCII
    Write-Host "enabled timescaledb in shared_preload_libraries"
}

& (Join-Path $pgBin 'pg_isready.exe') -h 127.0.0.1 -p $pgPort -q 2>$null
if ($LASTEXITCODE -ne 0) {
    & $pgCtl -D $pgData -l (Join-Path $pgRoot 'server.log') -o "-p $pgPort" -w start
    if ($LASTEXITCODE -ne 0) { throw "postgres failed to start - see $pgRoot\server.log" }
}

$dbExists = & $psql -h 127.0.0.1 -p $pgPort -U pico -d postgres -tAc "select 1 from pg_database where datname='pico_monitor'"
if ($dbExists -ne '1') {
    & (Join-Path $pgBin 'createdb.exe') -h 127.0.0.1 -p $pgPort -U pico pico_monitor
}
& $psql -h 127.0.0.1 -p $pgPort -U pico -d pico_monitor -q -f (Join-Path $repoRoot 'db\schema.sql')
if ($LASTEXITCODE -ne 0) { throw 'schema.sql failed' }
Write-Host "schema applied; psql -h 127.0.0.1 -p $pgPort -U pico -d pico_monitor"

# ------------------------------------------------- project dependencies ----
Step 'Collector + UI dependencies'
Push-Location (Join-Path $repoRoot 'server')
try { & npm install --no-audit --no-fund; if ($LASTEXITCODE -ne 0) { throw 'server npm install failed' } }
finally { Pop-Location }
Push-Location (Join-Path $repoRoot 'ui')
try {
    & npm install --no-audit --no-fund; if ($LASTEXITCODE -ne 0) { throw 'ui npm install failed' }
    & npm run build;       if ($LASTEXITCODE -ne 0) { throw 'ui build failed' }
}
finally { Pop-Location }

# ------------------------------------------------------------- service -----
if ($Service) {
    Step 'Logon task'
    $cmd = Join-Path $repoRoot 'scripts\collector.cmd'
    $action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"$cmd`"" -WorkingDirectory $repoRoot
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $taskSettings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName 'Pico2Bme280Collector' -Action $action -Trigger $trigger -Settings $taskSettings -Force | Out-Null
    Write-Host 'task registered: collector starts at logon (Pico connection starts measurement immediately)'
}

Step 'done'
@"
Start the collector with:
    scripts\collector.cmd
then open:
    http://127.0.0.1:8787
Connect the Pico 2 (BME280 wired) and measurement begins automatically.
"@
