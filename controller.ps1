#Requires -Version 5.1
<#
.SYNOPSIS
    Modern C++26 Cloud & Chat Sistemi icin REST API yonetim araci (oda + config).

.DESCRIPTION
    controller.ps1, calisan bir sunucunun REST API'sini (/api/rooms ve /api/config)
    yonetmek icin kullanilir: oda listeleme/olusturma/acma/kapatma/silme ve
    yapilandirma okuma/guncelleme/sifirlama. Postman veya elle curl calistirmaya
    gerek kalmadan, ayni istekleri hazir PowerShell komutlariyla gonderir.

    Bu betik build.ps1'den tamamen bagimsizdir: derleme/test adimlarina hic
    dokunmaz, yalnizca hedef sunucuya HTTP istegi atar. Sunucunun zaten
    calisiyor olmasi gerekir (bkz. .\build.ps1 -Server).

    Oda ve config yonetiminin TEK adresidir; ayni islevler build.ps1 icinde
    tekrar tanimlanmaz (kod tekrarini onlemek icin).

.PARAMETER Target
    Yonetilecek sunucunun IP/domain adresi. Varsayilan: 127.0.0.1.

.PARAMETER Port
    Yonetilecek sunucunun portu. Varsayilan: 8080.

.PARAMETER ListRooms
    Sunucudaki tum odalari listeler.

.PARAMETER CreateRoom
    Verilen isimde yeni bir oda olusturur.

.PARAMETER CloseRoom
    Verilen oda ID'sini veya adini kapatir (yeni baglantilara kapatir).

.PARAMETER OpenRoom
    Kapali bir odayi yeniden acar.

.PARAMETER DeleteRoom
    Odayi ve tum mesaj gecmisini kalici olarak siler.

.PARAMETER GetConfig
    Sunucunun mevcut REST yapilandirmasini okur.

.PARAMETER SetConfig
    -ConfigHost, -ConfigPort ve istege bagli -ConfigActive ile yapilandirmayi gunceller.

.PARAMETER ResetConfig
    Yapilandirmayi sunucu varsayilanlarina sifirlar.

.EXAMPLE
    .\controller.ps1 -ListRooms

.EXAMPLE
    .\controller.ps1 -CreateRoom "Genel Sohbet"

.EXAMPLE
    .\controller.ps1 -CloseRoom 3
    .\controller.ps1 -OpenRoom "Genel Sohbet"
    .\controller.ps1 -DeleteRoom 3

.EXAMPLE
    .\controller.ps1 -GetConfig
    .\controller.ps1 -SetConfig -ConfigHost "node1.internal" -ConfigPort 9000 -ConfigActive $true
    .\controller.ps1 -ResetConfig

.EXAMPLE
    .\controller.ps1 -ListRooms -Target 192.168.1.20 -Port 8080
    Farkli bir sunucuyu hedefler.

.NOTES
    Parametresiz calistirildiginda (.\controller.ps1) kullanim ozeti ekrana yazdirilir.
#>
[CmdletBinding()]
param (
    # Yonetilecek sunucunun adresi ve portu
    [Alias("HostAddress")]
    [string]$Target = "127.0.0.1",
    [int]$Port = 8080,

    # --- Oda (Room) Yonetimi ---
    [switch]$ListRooms,
    [string]$CreateRoom = "",
    [string]$CloseRoom = "",
    [string]$OpenRoom = "",
    [string]$DeleteRoom = "",

    # --- Config Yonetimi ---
    [switch]$GetConfig,
    [switch]$SetConfig,
    [switch]$ResetConfig,
    [string]$ConfigHost = "",
    [int]$ConfigPort = 0,
    [bool]$ConfigActive = $true,

    # Istek zaman asimi (saniye)
    [int]$TimeoutSec = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$BaseApiUrl = "http://${Target}:${Port}"

# -------------------------------------------------------------
# 1. Yardimci Fonksiyonlar
# -------------------------------------------------------------

# Sayisal bir oda ID'si veya oda adi alir; adi verilmisse GET /api/rooms ile ID'ye cozer.
function Resolve-TargetRoomId([string]$RoomIdentifier) {
    $parsedId = 0
    if ([int64]::TryParse($RoomIdentifier, [ref]$parsedId)) {
        return $parsedId
    }

    $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Get -TimeoutSec $TimeoutSec -ErrorAction Stop
    $found = $resp.rooms | Where-Object { $_.name -ieq $RoomIdentifier } | Select-Object -First 1
    if ($found) {
        return [int64]$found.id
    }

    Write-Error "Sunucuda '$RoomIdentifier' isimli bir oda bulunamadi."
    exit 1
}

# Invoke-RestMethod hatalarindan sunucunun dondurdugu govdeyi (varsa) okuyup gosterir.
function Write-ApiError($ErrorRecord) {
    $msg = $ErrorRecord.Exception.Message
    if ($ErrorRecord.Exception.Response) {
        try {
            $stream = $ErrorRecord.Exception.Response.GetResponseStream()
            $reader = New-Object System.IO.StreamReader($stream)
            $body = $reader.ReadToEnd()
            if ($body) {
                $msg = $body
            }
        }
        catch {
            # Govde okunamazsa orijinal mesaj ile devam edilir
        }
    }
    Write-Error "API Islemi Basarisiz: $msg"
}

# -------------------------------------------------------------
# 2. Oda (Room) Komutlari  ->  /api/rooms
# -------------------------------------------------------------
$IsRoomCommand = $ListRooms -or ($CreateRoom -ne "") -or ($CloseRoom -ne "") -or ($OpenRoom -ne "") -or ($DeleteRoom -ne "")

if ($IsRoomCommand) {
    try {
        if ($ListRooms) {
            Write-Host "`n>>> Odalar Listeleniyor ($BaseApiUrl/api/rooms)..." -ForegroundColor Cyan
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Get -TimeoutSec $TimeoutSec

            if ($resp.rooms.Count -eq 0) {
                Write-Host "Henuz hic oda olusturulmamis." -ForegroundColor Yellow
            }
            else {
                $resp.rooms | Format-Table -Property @(
                    @{Label = "ID"; Expression = { $_.id }; Width = 6 },
                    @{Label = "Oda Adi"; Expression = { $_.name }; Width = 25 },
                    @{Label = "Durum"; Expression = { if ($_.is_open) { "Acik" } else { "Kapali" } }; Width = 10 },
                    @{Label = "Aktif Baglanti"; Expression = { $_.active_ws_clients }; Width = 15 },
                    @{Label = "Olusturulma Tarihi"; Expression = { $_.created_at }; Width = 20 }
                )
            }
        }
        elseif ($CreateRoom -ne "") {
            Write-Host "`n>>> Yeni Oda Olusturuluyor: '$CreateRoom'..." -ForegroundColor Cyan
            $body = @{ name = $CreateRoom } | ConvertTo-Json
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Post -ContentType "application/json" -Body $body -TimeoutSec $TimeoutSec

            Write-Host "Oda basariyla olusturuldu!" -ForegroundColor Green
            Write-Host "Oda ID    : $($resp.room_id)" -ForegroundColor White
            Write-Host "Oda Adi   : $($resp.name)" -ForegroundColor White
            Write-Host "Tarih     : $($resp.created_at)" -ForegroundColor DarkGray
        }
        elseif ($CloseRoom -ne "") {
            $roomId = Resolve-TargetRoomId $CloseRoom
            Write-Host "`n>>> Oda Kapatiliyor (ID: #$roomId)..." -ForegroundColor Cyan
            $body = @{ is_open = $false } | ConvertTo-Json
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms/$roomId" -Method Put -ContentType "application/json" -Body $body -TimeoutSec $TimeoutSec

            Write-Host "Oda basariyla kapatildi (#$roomId - $($resp.name)). Yeni girisler engellendi." -ForegroundColor Yellow
        }
        elseif ($OpenRoom -ne "") {
            $roomId = Resolve-TargetRoomId $OpenRoom
            Write-Host "`n>>> Oda Yeniden Aciliyor (ID: #$roomId)..." -ForegroundColor Cyan
            $body = @{ is_open = $true } | ConvertTo-Json
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms/$roomId" -Method Put -ContentType "application/json" -Body $body -TimeoutSec $TimeoutSec

            Write-Host "Oda basariyla erisime acildi (#$roomId - $($resp.name))." -ForegroundColor Green
        }
        elseif ($DeleteRoom -ne "") {
            $roomId = Resolve-TargetRoomId $DeleteRoom
            Write-Host "`n>>> Oda Siliniyor (ID: #$roomId)..." -ForegroundColor Red
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms/$roomId" -Method Delete -TimeoutSec $TimeoutSec

            Write-Host "Oda ve tum sohbet gecmisi veritabanindan silindi (#$roomId)." -ForegroundColor Green
        }
    }
    catch {
        Write-ApiError $_
        exit 1
    }
    exit 0
}

# -------------------------------------------------------------
# 3. Config Komutlari  ->  /api/config
# -------------------------------------------------------------
$IsConfigCommand = $GetConfig -or $SetConfig -or $ResetConfig

if ($IsConfigCommand) {
    try {
        if ($GetConfig) {
            Write-Host "`n>>> Yapilandirma Okunuyor ($BaseApiUrl/api/config)..." -ForegroundColor Cyan
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/config" -Method Get -TimeoutSec $TimeoutSec
            $resp | Format-List
        }
        elseif ($SetConfig) {
            if ($ConfigHost -eq "" -or $ConfigPort -eq 0) {
                Write-Error "-SetConfig icin -ConfigHost ve -ConfigPort zorunludur (ornek: -SetConfig -ConfigHost node1.internal -ConfigPort 9000)."
                exit 1
            }

            Write-Host "`n>>> Yapilandirma Guncelleniyor ($BaseApiUrl/api/config)..." -ForegroundColor Cyan
            $body = @{ host = $ConfigHost; port = $ConfigPort; active = $ConfigActive } | ConvertTo-Json
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/config" -Method Post -ContentType "application/json" -Body $body -TimeoutSec $TimeoutSec

            Write-Host "Yapilandirma basariyla guncellendi!" -ForegroundColor Green
            $resp | Format-List
        }
        elseif ($ResetConfig) {
            Write-Host "`n>>> Yapilandirma Sifirlaniyor ($BaseApiUrl/api/config)..." -ForegroundColor Red
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/config" -Method Delete -TimeoutSec $TimeoutSec

            Write-Host "Yapilandirma sifirlandi." -ForegroundColor Green
            if ($resp) {
                $resp | Format-List
            }
        }
    }
    catch {
        Write-ApiError $_
        exit 1
    }
    exit 0
}

# -------------------------------------------------------------
# 4. Komut Verilmediyse Kullanim Bilgisi
# -------------------------------------------------------------
Write-Host "`n=== controller.ps1 - REST API Yonetim Araci (Hedef: $BaseApiUrl) ===" -ForegroundColor Cyan
Write-Host "Postman'a gerek kalmadan sunucu REST API'sini bu betik ile yonetebilirsiniz." -ForegroundColor DarkGray
Write-Host "Detayli parametre aciklamalari icin: Get-Help .\controller.ps1 -Full`n" -ForegroundColor DarkGray

Write-Host "Oda (Room) Komutlari:" -ForegroundColor Yellow
Write-Host "  .\controller.ps1 -ListRooms"
Write-Host "  .\controller.ps1 -CreateRoom 'Genel Sohbet'"
Write-Host "  .\controller.ps1 -CloseRoom 3            # veya -CloseRoom 'Genel Sohbet'"
Write-Host "  .\controller.ps1 -OpenRoom 3"
Write-Host "  .\controller.ps1 -DeleteRoom 3"

Write-Host "`nConfig Komutlari:" -ForegroundColor Yellow
Write-Host "  .\controller.ps1 -GetConfig"
Write-Host "  .\controller.ps1 -SetConfig -ConfigHost 'node1.internal' -ConfigPort 9000 -ConfigActive `$true"
Write-Host "  .\controller.ps1 -ResetConfig"

Write-Host "`nFarkli bir sunucuyu hedeflemek icin:" -ForegroundColor Yellow
Write-Host "  .\controller.ps1 -ListRooms -Target 192.168.1.20 -Port 8080"