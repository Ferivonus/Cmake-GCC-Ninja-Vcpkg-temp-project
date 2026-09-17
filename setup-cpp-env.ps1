#Requires -Version 5.1
#Requires -RunAsAdministrator

$ErrorActionPreference = "Stop"

function Write-Status($message)  { Write-Host "[*] $message" -ForegroundColor Cyan }
function Write-Success($message) { Write-Host "[+] $message" -ForegroundColor Green }
function Write-Warn($message)    { Write-Host "[!] $message" -ForegroundColor Yellow }

# 1. Ortam Değişkeni Güncelleyici (Kalıcı ve Geçerli Oturum)
function Add-To-UserPath($pathToAdd) {
    if (-not (Test-Path $pathToAdd)) { return }

    # Mevcut oturum PATH'ine ekle
    $sessionPaths = $env:PATH -split ';'
    if ($sessionPaths -notcontains $pathToAdd) {
        $env:PATH = "$pathToAdd;$env:PATH"
    }

    # Kalıcı User PATH'ine ekle
    $currentPath = [System.Environment]::GetEnvironmentVariable("PATH", "User")
    $userPaths = ($currentPath -split ';') | Where-Object { $_ -ne "" }
    if ($userPaths -notcontains $pathToAdd) {
        $newPath = ($userPaths + $pathToAdd) -join ';'
        [System.Environment]::SetEnvironmentVariable("PATH", $newPath, "User")
        Write-Success "'$pathToAdd' kalici kullanici PATH degiskenine eklendi."
    }
}

# WinGet symlink dizinini her ihtimale karsi PATH'e al
$wingetLinks = "$env:LOCALAPPDATA\Microsoft\WinGet\Links"
if (Test-Path $wingetLinks) { Add-To-UserPath $wingetLinks }

# 2. Git Kontrolü
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Warn "Git bulunamadi. winget uzerinden kuruluyor..."
    winget install --id Git.Git -e --source winget --accept-source-agreements --accept-package-agreements
    Add-To-UserPath "C:\Program Files\Git\cmd"
} else {
    Write-Success "Git zaten yuklu."
}

# 3. CMake Kontrolü
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Warn "CMake bulunamadi. winget uzerinden kuruluyor..."
    winget install --id Kitware.CMake -e --source winget --accept-source-agreements --accept-package-agreements
    Add-To-UserPath "C:\Program Files\CMake\bin"
} else {
    Write-Success "CMake zaten yuklu: $((cmake --version | Select-Object -First 1))"
}

# 4. Ninja Kontrolü
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Warn "Ninja bulunamadi. winget uzerinden kuruluyor..."
    winget install --id Ninja-build.Ninja -e --source winget --accept-source-agreements --accept-package-agreements
    Add-To-UserPath $wingetLinks
} else {
    Write-Success "Ninja zaten yuklu: $(ninja --version)"
}

# 5. GCC / MinGW Kontrolü
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Warn "GCC (g++) bulunamadi. WinLibs MinGW-w64 kuruluyor..."
    winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e --source winget --accept-source-agreements --accept-package-agreements
    
    $possibleGccPaths = @(
        $wingetLinks,
        "$env:LOCALAPPDATA\Programs\mingw64\bin",
        "C:\mingw64\bin"
    )
    foreach ($p in $possibleGccPaths) {
        if (Test-Path "$p\g++.exe") {
            Add-To-UserPath $p
            break
        }
    }
} else {
    Write-Success "GCC zaten yuklu: $((g++ --version | Select-Object -First 1))"
}

# 6. vcpkg Kontrolü ve Bootstrap
$vcpkgDir = "C:\vcpkg"
$vcpkgExe = Join-Path $vcpkgDir "vcpkg.exe"

if (-not (Test-Path $vcpkgExe)) {
    Write-Warn "vcpkg bulunamadi. '$vcpkgDir' altina klonlanip derleniyor..."
    
    if (-not (Test-Path $vcpkgDir)) {
        # NativeCommandError onlemi: Git stderr basinca betik kirilmasin
        $prevEAP = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        git clone https://github.com/microsoft/vcpkg.git $vcpkgDir
        $ErrorActionPreference = $prevEAP
    }
    
    Write-Status "bootstrap-vcpkg calistiriliyor..."
    Start-Process -FilePath (Join-Path $vcpkgDir "bootstrap-vcpkg.bat") -NoNewWindow -Wait
}

# VCPKG_ROOT tanimlamalari
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgDir, "User")
$env:VCPKG_ROOT = $vcpkgDir
Add-To-UserPath $vcpkgDir
Write-Success "VCPKG_ROOT = $vcpkgDir olarak tanimlandi."

# 7. Son Dogrulama Tablosu
Write-Host "`n=== Guncel Gelistirme Ortami Ozeti ===" -ForegroundColor Magenta
$tools = @("cmake", "ninja", "g++", "vcpkg")
foreach ($tool in $tools) {
    $cmd = Get-Command $tool -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Host "$($tool.PadRight(10)): [OK] -> $($cmd.Source)" -ForegroundColor Green
    } else {
        Write-Host "$($tool.PadRight(10)): [FAIL] Terminali yeniden baslatmaniz gerekebilir." -ForegroundColor Red
    }
}

Write-Host "`nKurulum tamamlandi. Ayni pencerede su komutlari calistirabilirsiniz:" -ForegroundColor Yellow
Write-Host "  cmake --preset gcc-ninja-debug"
Write-Host "  cmake --build build"
Write-Host "  .\build\test.exe`n"