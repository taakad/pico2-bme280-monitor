# Pico2 BME280 Monitor — MSI ビルドスクリプト (WiX Toolset 7)
#
# 使い方:
#   pwsh scripts/build-msi.ps1                # 完全ビルド(WiX の MSI リンクは Windows 必須)
#   pwsh scripts/build-msi.ps1 -CompileOnly   # .wixobj 生成まで(macOS/Linux で構文検証)
#
# 前提:
#   - .NET SDK と WiX global tool (`dotnet tool install -g Wix`)
#   - Windows では PATH に git。WiX v7 は EULA 同意が必要なため -acceptEula wix7 を常時付与する。
param(
    [string]$Version = "",
    [string]$OutDir = "dist",
    [switch]$CompileOnly
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# --- バージョン決定 (git describe から) -------------------------------------
if (-not $Version) {
    $Version = (git describe --tags --abbrev=0).TrimStart("v")
}
if (-not $Version) { throw "バージョンを決定できません。-Version を指定してください。" }
# MSI の ProductVersion は major.minor.build(3パート・数字のみ)が必要
$msiVersion = ($Version -split "-")[0]
$parts = $msiVersion.Split(".")
if ($parts.Count -eq 2) { $msiVersion = "$msiVersion.0" }
if ($parts.Count -gt 3) { $msiVersion = ($parts[0..2] -join ".") }

$baseName = "pico2-bme280-monitor-$Version"
$stage = Join-Path $OutDir "msi-stage"
$bundle = Join-Path $OutDir "$baseName"
$msi = Join-Path $OutDir "$baseName.msi"

# --- ステージング (package.sh と同じファイルセット) -------------------------
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$files = git ls-files
foreach ($f in $files) {
    $dest = Join-Path $stage $f
    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Force -Path $destDir | Out-Null }
    Copy-Item $f $dest
}
# プリビルド済み UF2 があれば同梱(firmware/uf2/ に配置)
$uf2 = "firmware/build/bme280_monitor.uf2"
if (Test-Path $uf2) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stage "firmware/uf2") | Out-Null
    Copy-Item $uf2 (Join-Path $stage "firmware/uf2/bme280_monitor.uf2")
}
# MSI ショートカットの宛先: installer/setup.cmd をバンドル直下に配置
Copy-Item "installer/setup.cmd" (Join-Path $stage "setup.cmd")

# zip バンドルと同じ形に整える: ステージ直下ではなく <baseName>/ 配下にまとめる
if (Test-Path $bundle) { Remove-Item -Recurse -Force $bundle }
New-Item -ItemType Directory -Force -Path $bundle | Out-Null
Copy-Item -Recurse (Join-Path $stage "*") $bundle
$stage = $bundle
# WiX は相対パスを .wxs の位置基準で解決するため、絶対パスに正規化する
$stage = (Resolve-Path $stage).Path

Write-Host "ステージング完了: $stage ($(( Get-ChildItem -Recurse -File $stage).Count) ファイル)"
Write-Host "MSI バージョン: $msiVersion (リリースバージョン: $Version)"

# --- WiX 呼び出し ------------------------------------------------------------
$wix = "wix"
if (Get-Command wix -ErrorAction SilentlyContinue) { $wix = "wix" }
elseif ($IsWindows -and (Test-Path "$env:USERPROFILE\.dotnet\tools\wix.exe")) { $wix = "$env:USERPROFILE\.dotnet\tools\wix.exe" }
elseif (Test-Path "$HOME/.dotnet/tools/wix") { $wix = "$HOME/.dotnet/tools/wix" }

$wixArgs = @(
    "build", "installer/product.wxs",
    "-arch", "x64",
    "-d", "StageDir=$stage",
    "-d", "ProductVersion=$msiVersion",
    "-acceptEula", "wix7"
)
if (-not $CompileOnly) {
    # Windows の WiX のみ Directory/@Name を受理する(検証ビルドでは省略)
    $wixArgs += @("-d", "FolderName=Pico2 BME280 Monitor", "-o", $msi)
} else {
    # WiX v7 は .wixobj 単体の出力を持たないため、.wixlib 出力でコンパイル検証する
    # (MSI のリンクは Windows の msi.dll が必要なため macOS/Linux では行えない)
    $wixArgs += @("-o", (Join-Path $OutDir "$baseName.wixlib"))
}

Write-Host "→ $wix $($wixArgs -join ' ')"
& $wix @wixArgs
if ($LASTEXITCODE -ne 0) { throw "wix build が失敗しました (exit $LASTEXITCODE)" }

if ($CompileOnly) {
    Write-Host "構文検証完了: $(Join-Path $OutDir "$baseName.wixlib")"
} else {
    Get-FileHash $msi -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $(Split-Path -Leaf $msi)" } |
        Out-File -Encoding ascii (Join-Path $OutDir "$baseName.msi.sha256")
    Write-Host "MSI 生成完了: $msi"
}
