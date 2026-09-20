#Requires -Version 5.1
<#
.SYNOPSIS
    Modern C++26 Cloud & Chat Sistemi icin derleme, test ve calistirma otomasyonu.

.DESCRIPTION
    build.ps1, projenin CMake/Ninja tabanli yapilandirma, derleme ve CTest adimlarini
    tek komutla yonetir; ardindan istenirse sunucuyu veya istemciyi baslatir.

    Bu betik YALNIZCA derleme ve calistirma (build & run) ile ilgilenir. Calisan bir
    sunucudaki odalari veya REST API yapilandirmasini yonetmek icin (Postman'a gerek
    kalmadan) ayri betik olan .\controller.ps1 kullanilir.

.PARAMETER Mode
    Derleme modu: "debug"/"d" veya "release"/"r". Varsayilan: debug.

.PARAMETER Server
    Belirtilirse, derleme sonrasi sunucu modunda calistirir.

.PARAMETER Client
    Belirtilirse, verilen kullanici adiyla istemci modunda calistirir.

.PARAMETER Target
    Istemcinin baglanacagi veya sunucu-adi cozumlemesi icin sorgulanacak hedef adres.

.PARAMETER Room
    Client modunda baslangicta baglanilacak oda (sayisal ID veya oda adi). Belirtilmezse lobi modunda baslar.

.EXAMPLE
    .\build.ps1
    Debug modunda derler ve birim testlerini calistirir.

.EXAMPLE
    .\build.ps1 -Server -Port 9000 -BindAddress 127.0.0.1
    Ozel port ve IP ile sunucuyu derleyip baslatir.

.EXAMPLE
    .\build.ps1 -Client Ahmet -Target 192.168.1.20 -Port 8080
    Belirtilen sunucuya lobi modunda baglanir.

.EXAMPLE
    .\build.ps1 -Client Ahmet -Room 2
    Belirtilen odaya dogrudan katilarak baslar.

.NOTES
    Oda/config yonetimi icin: .\controller.ps1 -ListRooms / -GetConfig vb.
#>
[CmdletBinding()]
param (
    [Parameter(Position = 0)]
    [ValidateSet("debug", "release", "d", "r")]
    [string]$Mode = "debug",

    [switch]$Server,
    [string]$Client = "",
    [switch]$Tor,
    [int]$Port = 8080,

    # Sunucunun dinleyecegi IP adresi
    [string]$BindAddress = "0.0.0.0",

    # Baglanilacak veya istek atilacak hedef adres
    [Alias("HostAddress")]
    [string]$Target = "127.0.0.1",

    # Client modunda baglanilacak oda (ID veya Oda Adi). Bos ise Lobi modunda baslar.
    [Alias("RoomId", "RoomName")]
    [string]$Room = "",

    [string]$ProxyHost = "127.0.0.1",
    [int]$ProxyPort = 9050,

    # Build Kontrol Bayraklari
    [switch]$SkipBuild,
    [switch]$Rebuild,
    [switch]$NoTest,
    [Alias("Clear")]
    [switch]$Clean,
    [Alias("ClearOnly")]
    [switch]$CleanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$BaseApiUrl = "http://${Target}:${Port}"

# -------------------------------------------------------------
# 1. Build & Run Modu Yapilandirmasi
# -------------------------------------------------------------
$Config = if ($Mode -in @("debug", "d")) { "debug" } else { "release" }
$Preset = "windows-$Config"
$BuildDir = Join-Path -Path $PSScriptRoot -ChildPath "build/$Config"
$ExePath = Join-Path -Path $BuildDir -ChildPath "main_app.exe"
$IsRunMode = $Server -or ($Client -ne "")
$DoClean = $Clean -or $CleanOnly

if ($SkipBuild) {
    $NeedsBuild = $false
}
elseif ($DoClean -or $Rebuild) {
    $NeedsBuild = $true
}
elseif ($IsRunMode) {
    $NeedsBuild = -not (Test-Path -Path $ExePath)
}
else {
    $NeedsBuild = $true
}

function Invoke-Step([string]$StepName, [scriptblock]$Action) {
    Write-Host "`n>>> [$($Config.ToUpper())]$StepName..." -ForegroundColor Cyan
    $Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    & $Action

    if ($LASTEXITCODE -ne 0) {
        $Stopwatch.Stop()
        Write-Error "Hata: '$StepName' adimi basarisiz oldu. Exit Code: $LASTEXITCODE"
        exit $LASTEXITCODE
    }

    $Stopwatch.Stop()
    Write-Host ">>> $StepName tamamlandi ($([math]::Round($Stopwatch.Elapsed.TotalSeconds, 2))s)" -ForegroundColor DarkGray
}

if ($NeedsBuild) {
    Get-Process -Name "main_app" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Warning "Calisan main_app (PID: $($_.Id)) tespit edildi. Kilit cakismasini onlemek icin sonlandiriliyor."
        Stop-Process -Id $_.Id -Force
    }
}

if ($DoClean) {
    if (Test-Path -Path $BuildDir) {
        Write-Host "Temiz derleme istendi: $BuildDir temizleniyor..." -ForegroundColor Yellow
        Get-ChildItem -Path $BuildDir -Exclude "vcpkg_installed" -Force | Remove-Item -Recurse -Force -ErrorAction Stop
    }
    if ($CleanOnly) {
        Write-Host "Temizlik tamamlandi. Cikis yapiliyor." -ForegroundColor Green
        exit 0
    }
}

if ($NeedsBuild) {
    Invoke-Step "CMake Yapilandirma ($Preset)" { cmake --preset $Preset }
    Invoke-Step "Derleme ($Preset)" { cmake --build --preset $Preset }
    if (-not $NoTest) {
        Invoke-Step "CTest Birim Testleri" { ctest --preset $Preset }
    }
}
else {
    if ($IsRunMode) {
        Write-Host "`n>>> Mevcut build kullaniliyor ($ExePath). Yeniden derleme yapilmadi." -ForegroundColor DarkGray
        Write-Host ">>> Zorla yeniden derlemek icin -Rebuild kullanabilirsiniz." -ForegroundColor DarkGray
    }
    else {
        Write-Host "`n>>> -SkipBuild kullanildi: configure/build/test adimlari atlaniyor." -ForegroundColor DarkYellow
    }
    if (-not (Test-Path -Path $ExePath)) {
        Write-Error "Calistirilabilir dosya bulunamadi: $ExePath`nOnce derleme yapmalisiniz."
        exit 1
    }
}

# -------------------------------------------------------------
# 2. Sunucu veya Istemciyi Calistirma
# -------------------------------------------------------------
if ($Server) {
    Write-Host "`n=== Sunucu Baslatiliyor (Dinlenen IP: $BindAddress, Port:$Port) ===" -ForegroundColor Green
    & $ExePath "server" $Port.ToString()$BindAddress
}
elseif ($Client -ne "") {
    $ClientArgs = @("client", $Client, $Target, $Port.ToString())

    if ($Room -ne "") {
        $ResolvedRoomId = -1$parsedId = 0

        if ([int64]::TryParse($Room, [ref]$parsedId)) {
            $ResolvedRoomId = $parsedId
        }
        else {
            $IsOnionAddress = $Target.EndsWith(".onion", [System.StringComparison]::OrdinalIgnoreCase)
            if (-not $IsOnionAddress -and -not$Tor) {
                try {
                    $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Get -TimeoutSec 2 -ErrorAction Stop
                    $foundRoom = $resp.rooms | Where-Object { $_.name -ieq $Room } | Select-Object -First 1

                    if ($foundRoom) {
                        $ResolvedRoomId = [int64]$foundRoom.id
                        Write-Host ">>> Oda adi eslesmesi: '$Room' -> ID: #$ResolvedRoomId" -ForegroundColor Cyan
                    }
                    else {
                        Write-Warning "Sunucuda '$Room' isimli oda bulunamadi! Lobi modunda baslatiliyor."
                    }
                }
                catch {
                    Write-Warning "Oda listesi alinamadi ($($_.Exception.Message)). Lobi modunda baslatiliyor."
                }
            }
            else {
                Write-Warning "Tor (.onion) uzerinden isim cozumleme desteklenmez. Lobi modunda baslatiliyor."
            }
        }

        if ($ResolvedRoomId -gt 0) {
            $ClientArgs += @("-r", $ResolvedRoomId.ToString())
        }
    }

    $IsOnionAddress = $Target.EndsWith(".onion", [System.StringComparison]::OrdinalIgnoreCase)
    $NeedsTor = $Tor -or $IsOnionAddress -or
    $PSBoundParameters.ContainsKey('ProxyHost') -or $PSBoundParameters.ContainsKey('ProxyPort')

    if ($NeedsTor) {
        $ClientArgs += @("--proxy-host", $ProxyHost, "--proxy-port", $ProxyPort.ToString())
        if ($Tor -or $IsOnionAddress) {
            $ClientArgs += "--tor"
        }
    }

    $TargetRoomInfo = if ($Room -ne "") { "Hedef Oda: $Room" } else { "Mod: Lobi (Odasiz)" }
    Write-Host "`n=== Istemci Baslatiliyor (Kullanici: $Client | Hedef: ${Target}:${Port} | $TargetRoomInfo | Tor: $NeedsTor) ===" -ForegroundColor Green
    & $ExePath @ClientArgs
}
else {
    Write-Host "`nDerleme basarili. Calistirmak icin -Server veya -Client kullanin. Oda/config yonetimi icin .\controller.ps1 komutunu kullanin." -ForegroundColor Green
}