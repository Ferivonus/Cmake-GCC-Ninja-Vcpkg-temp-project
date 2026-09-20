#Requires -Version 5.1
<#
.SYNOPSIS
    Modern C++26 Cloud & Chat Sistemi REST API Yonetim Araci.

.DESCRIPTION
    REST API (/api/rooms ve /api/config) ile iletisim kurar.
    Tum istekler merkezi HTTP motoru ve deklaratif hata yakalama ile yonetilir.
#>
[CmdletBinding()]
[System.Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingPlainTextForPassword', 'Password', Justification = 'CLI uzerinden duz metin parola aktarimi zorunludur')]
param (
    [Alias("HostAddress")]
    [string]$Target = "127.0.0.1",
    [int]$Port = 8080,

    # --- Oda (Room) Yonetimi ---
    [switch]$ListRooms,
    [string]$CreateRoom = "",
    
    [System.Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingPlainTextForPassword', 'Password', Justification = 'CLI uzerinden duz metin parola aktarimi zorunludur')]
    [Alias("Pass", "Key")]
    [string]$Password = "",
    
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
# 1. Hata Tanilama Motoru (Structured Error Diagnostics)
# -------------------------------------------------------------
function Write-CustomError([System.Management.Automation.ErrorRecord]$err) {
    $ex = $err.Exception
    
    Write-Host "`n========================= [HATA TANILAMA] =========================" -ForegroundColor Red

    # Durum 1: PowerShell Parametre Baglama / Sozdizimi Hatasi
    if ($ex -is [System.Management.Automation.ParameterBindingException]) {
        Write-Host "Kategori : PowerShell Parametre Baglama Hatasi (Parameter Binding)" -ForegroundColor Yellow
        Write-Host "Mesaj    : $($ex.Message)" -ForegroundColor White
        Write-Host "Teshis   : Bir parametre bayragi ile degisken bitismis olabilir (-Param`$Var)." -ForegroundColor Cyan
        if ($err.InvocationInfo) {
            Write-Host "Konum    : $($err.InvocationInfo.ScriptName):$($err.InvocationInfo.ScriptLineNumber)" -ForegroundColor DarkGray
            Write-Host "Kod      : $($err.InvocationInfo.Line.Trim())" -ForegroundColor DarkGray
        }
    }
    # Durum 2: Ag ve HTTP Hatalari (System.Net.WebException)
    elseif ($ex -is [System.Net.WebException]) {
        $webEx = [System.Net.WebException]$ex
        
        if ($null -ne $webEx.Response) {
            $httpResp = [System.Net.HttpWebResponse]$webEx.Response
            $statusCode = [int]$httpResp.StatusCode
            $statusDesc = $httpResp.StatusDescription
            
            Write-Host "Kategori : REST API HTTP Yanit Hatasi [$statusCode$statusDesc]" -ForegroundColor Yellow

            $responseBody = ""
            try {
                $stream = $webEx.Response.GetResponseStream()
                if ($stream) {
                    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
                    $responseBody = $reader.ReadToEnd()
                }
            }
            catch {}

            if (-not [string]::IsNullOrWhiteSpace($responseBody)) {
                try {
                    $jsonObj = $responseBody | ConvertFrom-Json
                    $formattedJson = $jsonObj | ConvertTo-Json -Depth 3
                    Write-Host "Sunucu Detayi :`n$formattedJson" -ForegroundColor White
                }
                catch {
                    Write-Host "Sunucu Detayi : $responseBody" -ForegroundColor White
                }
            }
            else {
                Write-Host "Mesaj    : $($ex.Message)" -ForegroundColor White
            }
        }
        else {
            Write-Host "Kategori : Ag Baglanti Hatasi (Baglanti Reddedildi / Connection Refused)" -ForegroundColor Yellow
            Write-Host "Hedef    : $BaseApiUrl" -ForegroundColor White
            Write-Host "Mesaj    : $($ex.Message)" -ForegroundColor White
            Write-Host "Teshis   : C++ HTTP backend ayakta degil veya belirtilen port kapali." -ForegroundColor Cyan
            Write-Host "Cozum    : Sunucunun '.\build.ps1 -Server' ile calistirildigini dogrulayin." -ForegroundColor Green
        }
    }
    # Durum 3: CoreCLR HttpRequestException (PS 7+)
    elseif ($ex.GetType().FullName -eq "System.Net.Http.HttpRequestException") {
        Write-Host "Kategori : HTTP Istek Hatasi" -ForegroundColor Yellow
        Write-Host "Mesaj    : $($ex.Message)" -ForegroundColor White
    }
    # Durum 4: Genel Calisma Zamani Istisnalari
    else {
        Write-Host "Kategori : Calisma Zamani Istisnasi ($($ex.GetType().Name))" -ForegroundColor Yellow
        Write-Host "Mesaj    : $($ex.Message)" -ForegroundColor White
        if ($err.InvocationInfo) {
            Write-Host "Konum    : $($err.InvocationInfo.ScriptName):$($err.InvocationInfo.ScriptLineNumber)" -ForegroundColor DarkGray
        }
    }

    Write-Host "==================================================================`n" -ForegroundColor Red
}

# -------------------------------------------------------------
# 2. Merkezi HTTP Istek Motoru
# -------------------------------------------------------------
function Send-ChatApiRequest {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory = $true)]
        [ValidateSet("GET", "POST", "PUT", "DELETE")]
        [string]$Method,

        [Parameter(Mandatory = $true)]
        [string]$Endpoint,

        [object]$Payload = $null
    )

    $requestParams = @{
        Uri         = "$BaseApiUrl$Endpoint"
        Method      = $Method
        TimeoutSec  = [int]$TimeoutSec
        ErrorAction = 'Stop'
    }

    if ($null -ne $Payload) {
        $requestParams["ContentType"] = "application/json; charset=utf-8"
        if ($Payload -is [string]) {
            $requestParams["Body"] = $Payload
        }
        else {
            # Parameter Binding hatasini onlemek icin pipeline kullanildi
            $requestParams["Body"] = ($Payload | ConvertTo-Json -Compress)
        }
    }

    return (Invoke-RestMethod @requestParams)
}

function Resolve-TargetRoomId([string]$RoomIdentifier) {
    $parsedId = 0
    if ([int64]::TryParse($RoomIdentifier, [ref]$parsedId)) {
        return $parsedId
    }

    $resp = Send-ChatApiRequest -Method "GET" -Endpoint "/api/rooms"
    $found = @($resp.rooms) | Where-Object { $_.name -ieq $RoomIdentifier } | Select-Object -First 1
    if ($found) {
        return [int64]$found.id
    }

    throw [System.InvalidOperationException]::new("Sunucuda '$RoomIdentifier' isimli bir oda bulunamadi.")
}

# -------------------------------------------------------------
# 3. Komut Isleme ve Yonlendirme
# -------------------------------------------------------------
try {
    $IsRoomCommand = $ListRooms -or ($CreateRoom -ne "") -or ($CloseRoom -ne "") -or ($OpenRoom -ne "") -or ($DeleteRoom -ne "")

    if ($IsRoomCommand) {
        if ($ListRooms) {
            Write-Host "`n>>> Odalar Listeleniyor ($BaseApiUrl/api/rooms)..." -ForegroundColor Cyan
            $resp = Send-ChatApiRequest -Method "GET" -Endpoint "/api/rooms"

            if (@($resp.rooms).Count -eq 0) {
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
            $resp = Send-ChatApiRequest -Method "POST" -Endpoint "/api/rooms" -Payload @{ name = $CreateRoom }

            $RoomKey = if ($Password -ne "") {
                $Password
            }
            else {
                [System.Guid]::NewGuid().ToString("N").Substring(0, 8)
            }

            Write-Host "Oda basariyla olusturuldu!" -ForegroundColor Green
            Write-Host "Oda ID        : $($resp.room_id)" -ForegroundColor White
            Write-Host "Oda Adi       : $($resp.name)" -ForegroundColor White
            Write-Host "Tarih         : $($resp.created_at)" -ForegroundColor DarkGray
            Write-Host "E2EE Anahtari : $RoomKey" -ForegroundColor Yellow

            Write-Host "`n>>> Paylasilabilir Katilim Komutlari:" -ForegroundColor Cyan
            Write-Host "  Terminalden Baglanti : .\build.ps1 -Client KullaniciAdi -Room $($resp.room_id) -Password `"$RoomKey`"" -ForegroundColor White
            Write-Host "  Sohbet Icinden Giris : /join $($resp.room_id) $RoomKey" -ForegroundColor White
        }
        elseif ($CloseRoom -ne "") {
            $roomId = Resolve-TargetRoomId -RoomIdentifier $CloseRoom
            Write-Host "`n>>> Oda Kapatiliyor (ID: #$roomId)..." -ForegroundColor Cyan
            $resp = Send-ChatApiRequest -Method "PUT" -Endpoint "/api/rooms/$roomId" -Payload @{ is_open = $false }

            Write-Host "Oda basariyla kapatildi (#$roomId - $($resp.name)). Yeni girisler engellendi." -ForegroundColor Yellow
        }
        elseif ($OpenRoom -ne "") {
            $roomId = Resolve-TargetRoomId -RoomIdentifier$OpenRoom
            Write-Host "`n>>> Oda Yeniden Aciliyor (ID: #$roomId)..." -ForegroundColor Cyan
            $resp = Send-ChatApiRequest -Method "PUT" -Endpoint "/api/rooms/$roomId" -Payload @{ is_open = $true }

            Write-Host "Oda basariyla erisime acildi (#$roomId - $($resp.name))." -ForegroundColor Green
        }
        elseif ($DeleteRoom -ne "") {
            $roomId = Resolve-TargetRoomId -RoomIdentifier $DeleteRoom
            Write-Host "`n>>> Oda Siliniyor (ID: #$roomId)..." -ForegroundColor Red
            $resp = Send-ChatApiRequest -Method "DELETE" -Endpoint "/api/rooms/$roomId"

            Write-Host "Oda ve tum sohbet gecmisi veritabanindan silindi (#$roomId)." -ForegroundColor Green
        }
        exit 0
    }

    $IsConfigCommand = $GetConfig -or $SetConfig -or $ResetConfig

    if ($IsConfigCommand) {
        if ($GetConfig) {
            Write-Host "`n>>> Yapilandirma Okunuyor ($BaseApiUrl/api/config)..." -ForegroundColor Cyan
            $resp = Send-ChatApiRequest -Method "GET" -Endpoint "/api/config"
            $resp | Format-List
        }
        elseif ($SetConfig) {
            if ($ConfigHost -eq "" -or $ConfigPort -eq 0) {
                throw [System.ArgumentException]::new("-SetConfig icin -ConfigHost ve -ConfigPort parametreleri zorunludur.")
            }

            Write-Host "`n>>> Yapilandirma Guncelleniyor ($BaseApiUrl/api/config)..." -ForegroundColor Cyan
            $payload = @{ host = $ConfigHost; port = $ConfigPort; active = $ConfigActive }
            $resp = Send-ChatApiRequest -Method "POST" -Endpoint "/api/config" -Payload $payload

            Write-Host "Yapilandirma basariyla guncellendi!" -ForegroundColor Green
            $resp | Format-List
        }
        elseif ($ResetConfig) {
            Write-Host "`n>>> Yapilandirma Sifirlaniyor ($BaseApiUrl/api/config)..." -ForegroundColor Red
            $resp = Send-ChatApiRequest -Method "DELETE" -Endpoint "/api/config"

            Write-Host "Yapilandirma sifirlandi." -ForegroundColor Green
            if ($resp) {
                $resp | Format-List
            }
        }
        exit 0
    }

    # Hicbir parametre verilmediyse kullanim yardimi goster
    Write-Host "`n=== controller.ps1 - REST API Yonetim Araci (Hedef: $BaseApiUrl) ===" -ForegroundColor Cyan
    Write-Host "`nOda (Room) Komutlari:" -ForegroundColor Yellow
    Write-Host "  .\controller.ps1 -ListRooms"
    Write-Host "  .\controller.ps1 -CreateRoom 'Gizli Sohbet'"
    Write-Host "  .\controller.ps1 -CreateRoom 'Ozel Oda' -Password 'gizli123'"
    Write-Host "  .\controller.ps1 -CloseRoom 3"
    Write-Host "  .\controller.ps1 -OpenRoom 3"
    Write-Host "  .\controller.ps1 -DeleteRoom 3"

    Write-Host "`nYapilandirma (Config) Komutlari:" -ForegroundColor Yellow
    Write-Host "  .\controller.ps1 -GetConfig"
    Write-Host "  .\controller.ps1 -SetConfig -ConfigHost 127.0.0.1 -ConfigPort 8080"
    Write-Host "  .\controller.ps1 -ResetConfig"
}
catch {
    Write-CustomError $_
    exit 1
}