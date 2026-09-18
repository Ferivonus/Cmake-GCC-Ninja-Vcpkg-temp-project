#Requires -Version 5.1
[CmdletBinding(DefaultParameterSetName = "BuildAndRun")]
param (
    [Parameter(Position = 0)]
    [ValidateSet("debug", "release", "d", "r")]
    [string]$Mode = "debug",

    [Parameter(ParameterSetName = "BuildAndRun")]
    [switch]$Server,

    [Parameter(ParameterSetName = "BuildAndRun")]
    [string]$Client = "",

    [Parameter(ParameterSetName = "BuildAndRun")]
    [switch]$Tor,

    [Parameter(ParameterSetName = "BuildAndRun")]
    [int]$Port = 8080,

    # Sunucunun DINLEYECEGI adres (server modu icin). Varsayilan: tum arayuzler.
    [Parameter(ParameterSetName = "BuildAndRun")]
    [string]$BindAddress = "0.0.0.0",

    # Client'in BAGLANACAGI hedef (client modu icin). IP, domain veya *.onion adresi buraya girilir.
    [Parameter(ParameterSetName = "BuildAndRun")]
    [Alias("HostAddress")]
    [string]$Target = "127.0.0.1",

    [Parameter(ParameterSetName = "BuildAndRun")]
    [string]$ProxyHost = "127.0.0.1",

    [Parameter(ParameterSetName = "BuildAndRun")]
    [int]$ProxyPort = 9050,

    # Build/Configure/Test adimlarini zorla atlayip dogrudan mevcut exe'yi calistirir.
    [switch]$SkipBuild,
    # -Server/-Client ile calisirken exe zaten varsa da build'i zorla tekrar alir.
    [switch]$Rebuild,
    [switch]$NoTest,
    [switch]$Clean,
    [switch]$CleanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Preset & Path Cozumleme
$Config    = if ($Mode -in @("debug", "d")) { "debug" } else { "release" }
$Preset    = "windows-$Config"
$BuildDir  = Join-Path -Path $PSScriptRoot -ChildPath "build/$Config"
$ExePath   = Join-Path -Path $BuildDir -ChildPath "main_app.exe"
$IsRunMode = $Server -or ($Client -ne "")
$DoClean   = $Clean -or $CleanOnly

# Build alinip alinmayacagina karar ver:
#  - -SkipBuild verildiyse: asla build alma (exe yoksa asagida hata verilir).
#  - Temizlik istendiyse veya -Rebuild verildiyse: her zaman build al.
#  - -Server / -Client ile calistiriliyorsa: exe zaten varsa DOKUNMA, sadece calistir.
#    (Boylece calisan bir server'i, ayni exe'yi client icin de kullanirken
#     yanlislikla kilitleyip kapatmayiz.)
#  - Duz `.\build.ps1` (server/client belirtilmeden) cagrisi: her zaman build+test akisidir.
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
    Write-Host "`n>>> [$($Config.ToUpper())] $StepName..." -ForegroundColor Cyan
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

# 0. Calisan Hedef Process Denetimi (LNK1104 / Permission Denied Korumasi)
# Sadece gercekten build alinacaksa (yeniden derleme/temizlik) calisan main_app'i kapatiyoruz.
# Sadece calistirma yapiliyorsa (exe zaten guncel) BURAYA HIC GIRILMEZ,
# boylece ornegin server acikken ayri bir pencerede client baslatmak onu kapatmaz.
if ($NeedsBuild) {
    Get-Process -Name "main_app" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Warning "Calisan main_app (PID: $($_.Id)) tespit edildi. Kilit cakismasini onlemek icin sonlandiriliyor."
        Stop-Process -Id $_.Id -Force
    }
}

# 1. Clean Islemi (vcpkg dizini izole edilmisse hedef odakli silme)
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
    # 2. CMake Configure
    Invoke-Step "CMake Yapilandirma ($Preset)" {
        cmake --preset $Preset
    }

    # 3. Build (Ninja / GCC)
    Invoke-Step "Derleme ($Preset)" {
        cmake --build --preset $Preset
    }

    # 4. Birim Testleri (CTest)
    if (-not $NoTest) {
        Invoke-Step "CTest Birim Testleri" {
            ctest --preset $Preset
        }
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
        Write-Error "Calistirilabilir dosya bulunamadi: $ExePath`nOnce -SkipBuild/-Rebuild olmadan bir kez build almalisin."
        exit 1
    }
}

# 5. Calistirma (Run)
if ($Server) {
    Write-Host "`n=== Sunucu Baslatiliyor (Dinlenen IP: $BindAddress, Port: $Port) ===" -ForegroundColor Green
    & $ExePath "server" $Port.ToString() $BindAddress
}
elseif ($Client -ne "") {
    $ClientArgs = @("client", $Client, $Target, $Port.ToString())

    # Tor gereksinimi: Explicit switch, .onion adresi veya belirtilen proxy parametreleri
    $IsOnionAddress = $Target.EndsWith(".onion", [System.StringComparison]::OrdinalIgnoreCase)
    $NeedsTor = $Tor -or $IsOnionAddress -or
                $PSBoundParameters.ContainsKey('ProxyHost') -or $PSBoundParameters.ContainsKey('ProxyPort')

    if ($NeedsTor) {
        $ClientArgs += @("--proxy-host", $ProxyHost, "--proxy-port", $ProxyPort.ToString())
        if ($Tor -or $IsOnionAddress) {
            $ClientArgs += "--tor"
        }
    }

    Write-Host "`n=== Istemci Baslatiliyor (Kullanici: $Client | Hedef: ${Target}:${Port} | Tor: $NeedsTor) ===" -ForegroundColor Green
    & $ExePath @ClientArgs
}
else {
    Write-Host "`nDerleme basarili. Calistirmak icin -Server veya -Client parametresi ile cagirin." -ForegroundColor Green
}