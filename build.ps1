[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("debug", "release", "d", "r")]
    [string]$Mode = "debug",

    [switch]$Server,
    [string]$Client = "",
    [switch]$Tor,
    [int]$Port = 8080,     [string]$HostAddress = "127.0.0.1",
    [string]$ProxyHost = "127.0.0.1",
    [int]$ProxyPort = 9050,     [switch]$NoTest,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Mod normalizasyonu
if ($Mode -in @("debug", "d")) {
    $Config = "debug"
    $Preset = "windows-debug"
} else {
    $Config = "release"
    $Preset = "windows-release"
}

$BuildDir = "build/$Config"
$ExePath = "$BuildDir/main_app.exe"

Write-Host "=== [$($Config.ToUpper())] Hazirlik ve Yapilandirma ===" -ForegroundColor Cyan

if ($Clean -and (Test-Path$BuildDir)) {
    Write-Host "Temiz derleme istenildi. Dizin temizleniyor: $BuildDir" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# 1. CMake Yapılandırması (Configure)
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    Write-Host "Yapilandirma (CMake) basarisiz oldu!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 2. Derleme (Build)
Write-Host "`n=== Derleme Basliyor: $BuildDir ===" -ForegroundColor Cyan
cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) {
    Write-Host "Derleme (Ninja/GCC) basarisiz oldu!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 3. Test (CTest)
if (-not $NoTest) {
    Write-Host "`n=== Testler Kosturuluyor (CTest) ===" -ForegroundColor Cyan
    ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Birim testler basarisiz! Uygulama baslatilmiyor." -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# 4. Calistirma (Run)
if ($Server) {
    Write-Host "`n=== Sunucu Baslatiliyor (Port: $Port) ===" -ForegroundColor Green
    & $ExePath server $Port
}
elseif ($Client -ne "") {
    $ClientArgs = @("client", $Client, $HostAddress, $Port.ToString())
    if ($Tor) {
        $ClientArgs += "--tor"
        $ClientArgs += "--proxy-host"
        $ClientArgs += $ProxyHost
        $ClientArgs += "--proxy-port"
        $ClientArgs += $ProxyPort.ToString()
    }
    Write-Host "`n=== Istemci Baslatiliyor (Kullanici: $Client, Tor:$Tor) ===" -ForegroundColor Green
    & $ExePath @ClientArgs
}
else {
    Write-Host "`nIslem basariyla tamamlandi. (Calistirmak icin -Server veya -Client parametresi ekleyin)" -ForegroundColor Green
}