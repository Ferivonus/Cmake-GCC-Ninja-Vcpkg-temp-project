#Requires -Version 5.1
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

    # Client modunda baglanilacak oda (ID veya Oda Adi)
    [Alias("RoomId", "RoomName")]
    [string]$Room = "1",

    [string]$ProxyHost = "127.0.0.1",
    [int]$ProxyPort = 9050,

    # --- REST API Yonetim Parametreleri ---
    [switch]$ListRooms,
    [string]$CreateRoom = "",
    [string]$CloseRoom = "",
    [string]$OpenRoom = "",
    [string]$DeleteRoom = "",

    # Build Kontrol Bayraklari
    [switch]$SkipBuild,
    [switch]$Rebuild,
    [switch]$NoTest,
    [switch]$Clean,
    [switch]$CleanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$BaseApiUrl = "http://${Target}:${Port}"

# -------------------------------------------------------------
# 1. API Yonetim Yardimci Fonksiyonlari
# -------------------------------------------------------------
function Resolve-TargetRoomId([string]$RoomIdentifier) {
    $parsedId = 0
    if ([int64]::TryParse($RoomIdentifier, [ref]$parsedId)) {
        return $parsedId
    }

    try {
        $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Get -TimeoutSec 3 -ErrorAction Stop
        $found = $resp.rooms | Where-Object { $_.name -ieq $RoomIdentifier } | Select-Object -First 1
        if ($found) {
            return [int64]$found.id
        }
        Write-Error "Sunucuda '$RoomIdentifier' isimli bir oda bulunamadi."
        exit 1
    }
    catch {
        Write-Error "Oda listesi alinamadi ($($_.Exception.Message)). Sunucunun acik oldugundan emin olun."
        exit 1
    }
}

# -------------------------------------------------------------
# 2. API Modu Tetiklendiyse Dogrudan Calis ve Cik
# -------------------------------------------------------------
$IsApiMode = $ListRooms -or ($CreateRoom -ne "") -or ($CloseRoom -ne "") -or ($OpenRoom -ne "") -or ($DeleteRoom -ne "")

if ($IsApiMode) {
    try {
        if ($ListRooms) {
            Write-Host "`n>>> Odalar Listeleniyor ($BaseApiUrl/api/rooms)..." -ForegroundColor Cyan
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Get -TimeoutSec 3
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
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms" -Method Post -ContentType "application/json" -Body $body -TimeoutSec 3
            Write-Host "Oda basariyla olusturuldu!" -ForegroundColor Green
            Write-Host "Oda ID    : $($resp.room_id)" -ForegroundColor White
            Write-Host "Oda Adi   : $($resp.name)" -ForegroundColor White
            Write-Host "Tarih     : $($resp.created_at)" -ForegroundColor DarkGray
        }
        elseif ($CloseRoom -ne "") {
            $roomId = Resolve-TargetRoomId $CloseRoom
            Write-Host "`n>>> Oda Kapatiliyor (ID: #$roomId)..." -ForegroundColor Cyan
            $body = @{ is_open = $false } | ConvertTo-Json
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms/$roomId" -Method Put -ContentType "application/json" -Body $body -TimeoutSec 3
            Write-Host "Oda basariyla kapatildi (#$roomId - $($resp.name)). Yeni girisler engellendi." -ForegroundColor Yellow
        }
        elseif ($OpenRoom -ne "") {
            $roomId = Resolve-TargetRoomId $OpenRoom
            Write-Host "`n>>> Oda Yeniden Aciliyor (ID: #$roomId)..." -ForegroundColor Cyan
            $body = @{ is_open = $true } | ConvertTo-Json
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms/$roomId" -Method Put -ContentType "application/json" -Body $body -TimeoutSec 3
            Write-Host "Oda basariyla erisime acildi (#$roomId - $($resp.name))." -ForegroundColor Green
        }
        elseif ($DeleteRoom -ne "") {
            $roomId = Resolve-TargetRoomId $DeleteRoom
            Write-Host "`n>>> Oda Siliniyor (ID: #$roomId)..." -ForegroundColor Red
            $resp = Invoke-RestMethod -Uri "$BaseApiUrl/api/rooms/$roomId" -Method Delete -TimeoutSec 3
            Write-Host "Oda ve tum sohbet gecmisi veritabanindan silindi (#$roomId)." -ForegroundColor Green
        }
    }
    catch {
        $msg = $_.Exception.Message
        if ($_.Exception.Response) {
            $stream = $_.Exception.Response.GetResponseStream()
            $reader = New-Object System.IO.StreamReader($stream)
            $msg = $reader.ReadToEnd()
        }
        Write-Error "API Islemi Basarisiz: $msg"
    }
    exit 0
}

# -------------------------------------------------------------
# 3. Build & Run Modu Yapilandirmasi
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
# 4. Sunucu veya Istemciyi Calistirma
# -------------------------------------------------------------
if ($Server) {
    Write-Host "`n=== Sunucu Baslatiliyor (Dinlenen IP: $BindAddress, Port:$Port) ===" -ForegroundColor Green
    & $ExePath "server" $Port.ToString() $BindAddress
}
elseif ($Client -ne "") {
    $ResolvedRoomId = 1
    $parsedId = 0

    if ([int64]::TryParse($Room, [ref]$parsedId)) {
        $ResolvedRoomId = $parsedId
    }
    else {
        $IsOnionAddress = $Target.EndsWith(".onion", [System.StringComparison]::OrdinalIgnoreCase)
        if (-not $IsOnionAddress -and -not $Tor) {
            try {
                $apiUrl = "http://${Target}:${Port}/api/rooms"
                $resp = Invoke-RestMethod -Uri $apiUrl -Method Get -TimeoutSec 2 -ErrorAction Stop
                $foundRoom = $resp.rooms | Where-Object { $_.name -ieq $Room } | Select-Object -First 1

                if ($foundRoom) {
                    $ResolvedRoomId = [int64]$foundRoom.id
                    Write-Host ">>> Oda adi eslesmesi: '$Room' -> ID: #$ResolvedRoomId" -ForegroundColor Cyan
                }
                else {
                    Write-Warning "Sunucuda '$Room' isimli oda bulunamadi! Varsayilan olarak #1 kullanilacak."
                }
            }
            catch {
                Write-Warning "Oda listesi REST API uzerinden alinamadi ($($_.Exception.Message)). Varsayilan #1 secildi."
            }
        }
        else {
            Write-Warning "Tor (.onion) uzerinden isim cozumleme desteklenmez. Sayisal oda ID'si gereklidir. Varsayilan #1 secildi."
        }
    }

    $ClientArgs = @("client", $Client, $Target, $Port.ToString(), "-r", $ResolvedRoomId.ToString())

    $IsOnionAddress = $Target.EndsWith(".onion", [System.StringComparison]::OrdinalIgnoreCase)
    $NeedsTor = $Tor -or $IsOnionAddress -or
    $PSBoundParameters.ContainsKey('ProxyHost') -or $PSBoundParameters.ContainsKey('ProxyPort')

    if ($NeedsTor) {
        $ClientArgs += @("--proxy-host", $ProxyHost, "--proxy-port", $ProxyPort.ToString())
        if ($Tor -or $IsOnionAddress) {
            $ClientArgs += "--tor"
        }
    }

    Write-Host "`n=== Istemci Baslatiliyor (Kullanici: $Client | Hedef: ${Target}:${Port} | Oda: #$ResolvedRoomId | Tor: $NeedsTor) ===" -ForegroundColor Green
    & $ExePath @ClientArgs
}
else {
    Write-Host "`nDerleme basarili. Calistirmak icin -Server, -Client veya API komutlarini (-ListRooms, -CreateRoom vb.) kullanin." -ForegroundColor Green
}