#Requires -Version 5.1
#Requires -RunAsAdministrator

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Status([string]$msg)  { Write-Host "[*] $msg" -ForegroundColor Cyan }
function Write-Success([string]$msg) { Write-Host "[+] $msg" -ForegroundColor Green }
function Write-Warn([string]$msg)    { Write-Host "[!] $msg" -ForegroundColor Yellow }

# Registry uzerindeki sistem ve kullanici PATH degiskenlerini anlik oturuma senkronize eder
function Update-SessionEnvironment {
    $machinePath = [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
    $userPath    = [System.Environment]::GetEnvironmentVariable("PATH", "User")
    
    $rawPaths = @($machinePath -split ';') + @($userPath -split ';')
    $uniquePaths = $rawPaths | Where-Object { [string]::IsNullOrWhiteSpace($_) -eq $false } | Select-Object -Unique
    
    $env:PATH = $uniquePaths -join ';'
}

# Sistem genelinde (Machine) ve gecerli oturumda PATH gunceller
function Add-To-MachinePath([string]$pathToAdd) {
    if (-not (Test-Path -LiteralPath $pathToAdd)) { return }

    # Oturum PATH kontrolu
    $sessionPaths = $env:PATH -split ';'
    if ($sessionPaths -notcontains $pathToAdd) {
        $env:PATH = "$pathToAdd;$env:PATH"
    }

    # Registry (Machine) kontrolu
    $currentMachinePath = [System.Environment]::GetEnvironmentVariable("PATH", "Machine")
    $machinePaths = ($currentMachinePath -split ';') | Where-Object { [string]::IsNullOrWhiteSpace($_) -eq $false }
    
    if ($machinePaths -notcontains $pathToAdd) {
        $newPath = (@($pathToAdd) + $machinePaths) -join ';'
        [System.Environment]::SetEnvironmentVariable("PATH", $newPath, "Machine")
        Write-Success "'$pathToAdd' sistem PATH degiskenine eklendi."
    }
}

# Yerel komut calistirici (NativeCommandError zaafini engeller)
function Invoke-NativeTool([scriptblock]$CommandBlock, [string]$ErrorMessage) {
    $prevEAP = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        & $CommandBlock
        if ($LASTEXITCODE -ne 0) {
            throw "$ErrorMessage (Exit Code: $LASTEXITCODE)"
        }
    }
    finally {
        $ErrorActionPreference = $prevEAP
    }
}

# WinGet symlink dizinlerini tanimla
$wingetLinks = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Links"
$wingetProgLinks = Join-Path $env:ProgramFiles "WinGet\Links"
if (Test-Path -LiteralPath $wingetLinks) { Add-To-MachinePath $wingetLinks }
if (Test-Path -LiteralPath $wingetProgLinks) { Add-To-MachinePath $wingetProgLinks }

# 1. Git Denetimi
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Warn "Git bulunamadi. winget uzerinden kuruluyor..."
    Invoke-NativeTool {
        winget install --id Git.Git -e --source winget --accept-source-agreements --accept-package-agreements
    } "Git kurulumu basarisiz oldu."
    Update-SessionEnvironment
    Add-To-MachinePath "C:\Program Files\Git\cmd"
} else {
    Write-Success "Git zaten yuklu: $((git --version))"
}

# 2. CMake Denetimi
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Warn "CMake bulunamadi. winget uzerinden kuruluyor..."
    Invoke-NativeTool {
        winget install --id Kitware.CMake -e --source winget --accept-source-agreements --accept-package-agreements
    } "CMake kurulumu basarisiz oldu."
    Update-SessionEnvironment
    Add-To-MachinePath "C:\Program Files\CMake\bin"
} else {
    Write-Success "CMake zaten yuklu: $((cmake --version | Select-Object -First 1))"
}

# 3. Ninja Denetimi
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) {
    Write-Warn "Ninja bulunamadi. winget uzerinden kuruluyor..."
    Invoke-NativeTool {
        winget install --id Ninja-build.Ninja -e --source winget --accept-source-agreements --accept-package-agreements
    } "Ninja kurulumu basarisiz oldu."
    Update-SessionEnvironment
    Add-To-MachinePath $wingetLinks
} else {
    Write-Success "Ninja zaten yuklu: $(ninja --version)"
}

# 4. GCC / MinGW Denetimi (WinLibs POSIX UCRT)
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Warn "GCC (g++) bulunamadi. WinLibs MinGW-w64 kuruluyor..."
    Invoke-NativeTool {
        winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e --source winget --accept-source-agreements --accept-package-agreements
    } "WinLibs kurulumu basarisiz oldu."
    Update-SessionEnvironment

    $possibleGccPaths = @(
        $wingetLinks,
        "$env:ProgramFiles\winlibs*\bin",
        "$env:LOCALAPPDATA\Programs\winlibs*\bin",
        "$env:LOCALAPPDATA\Programs\mingw64\bin",
        "C:\mingw64\bin"
    )

    foreach ($pattern in $possibleGccPaths) {
        $resolvedMatches = Resolve-Path -Path $pattern -ErrorAction SilentlyContinue
        if ($resolvedMatches) {
            foreach ($match in $resolvedMatches) {
                if (Test-Path -LiteralPath (Join-Path $match.Path "g++.exe")) {
                    Add-To-MachinePath $match.Path
                    break
                }
            }
        }
    }
} else {
    Write-Success "GCC zaten yuklu: $((g++ --version | Select-Object -First 1))"
}

# 5. vcpkg Kurulumu ve Konfigurasyonu
$vcpkgDir = "C:\vcpkg"
$vcpkgExe = Join-Path $vcpkgDir "vcpkg.exe"

if (-not (Test-Path -LiteralPath $vcpkgExe)) {
    Write-Warn "vcpkg bulunamadi. '$vcpkgDir' hedefine klonlanip bootstrap ediliyor..."
    
    if (-not (Test-Path -LiteralPath $vcpkgDir)) {
        Invoke-NativeTool {
            git clone https://github.com/microsoft/vcpkg.git $vcpkgDir
        } "vcpkg reposu klonlanamadi."
    }
    
    Write-Status "bootstrap-vcpkg calistiriliyor..."
    $bootstrapBat = Join-Path $vcpkgDir "bootstrap-vcpkg.bat"
    $process = Start-Process -FilePath $bootstrapBat -ArgumentList "-disableMetrics" -NoNewWindow -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "vcpkg bootstrap adimi basarisiz oldu. Exit Code: $($process.ExitCode)"
    }
}

# Sistem geneline VCPKG_ROOT tanimla (CMakePresets.json bu degiskene baglidir)
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgDir, "Machine")
$env:VCPKG_ROOT = $vcpkgDir
Add-To-MachinePath $vcpkgDir
Write-Success "VCPKG_ROOT = '$vcpkgDir' (Machine) olarak tanimlandi."

Update-SessionEnvironment

# 6. Ortam Dogrulama Matrisi
Write-Host "`n=== Gelistirme Ortami Dogrulama Matrisi ===" -ForegroundColor Magenta
$tools = @("git", "cmake", "ninja", "g++", "vcpkg")
foreach ($tool in$tools) {
    $cmd = Get-Command$tool -ErrorAction SilentlyContinue
    if ($cmd) {
        Write-Host "$($tool.PadRight(10)): [OK]   -> $($cmd.Source)" -ForegroundColor Green
    } else {
        Write-Host "$($tool.PadRight(10)): [FAIL] -> Yeni bir PowerShell oturumu acmaniz gerekebilir." -ForegroundColor Red
    }
}

Write-Host "`nKurulum tamamlandi. Ilgili terminali yeniden baslatip '.\build.ps1' calistirabilirsiniz.`n" -ForegroundColor Yellow