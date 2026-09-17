# Modern C++26 Proje Şablonu

Bu proje; C++26 standardı, CMake, Ninja, GCC ve vcpkg entegrasyonuyla geliştirilmiş cross-platform (Windows / Linux) bir uygulama şablonudur. Bünyesinde JSON serileştirme (nlohmann-json), asenkron ağ ve HTTP/1.1 iletişimi (Boost.Beast / Asio), yapılandırılmış loglama (spdlog) ve birim test altyapısı (Catch) barındırır.

## Gereksinimler

- **GCC (g++ 15+):** C++26 standart desteği için
- **CMake 3.25+:** Modern hedef tabanlı yapılandırma ve C++26 desteği için
- **Ninja:** Hızlı ve paralel inşa motoru
- **vcpkg:** Manifest modunda (`vcpkg.json`) paket ve bağımlılık yönetimi için
- **Git:** vcpkg kurulumu ve paket indirmeleri için

---

## 1. Geliştirme Ortamı Kurulumu (Windows)

Gerekli araçlar sisteminizde kurulu değilse, **Yönetici olarak açılmış** bir PowerShell penceresinde ortam hazırlama betiğini çalıştırın:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\setup-cpp-env.ps1
```

---

## 2. Yapılandırma (Configure)

İşletim sisteminize uygun preset ile derleme dosyalarını hazırlayın:

**Windows (MinGW GCC):**
```powershell
cmake --preset windows-debug
```

**Linux (GCC):**
```bash
cmake --preset linux-debug
```

---

## 3. Derleme (Build)

Uygulamayı ve test süitini paralel olarak derler:

```bash
cmake --build build
```

---

## 4. Testleri Koşturma (Test)

Catch2 ile yazılmış birim testleri CTest üzerinden çalıştırır:

```bash
ctest --test-dir build --output-on-failure
```

---

## 5. Çalıştırma (Run)

Ana uygulamayı başlatır:

**Windows:**
```powershell
.\build\main_app.exe
```

**Linux:**
```bash
./build/main_app
```

---

## 6. Hızlı Geliştirme Döngüsü (Tek Satır)

Derleme, otomatik test ve çalıştırma adımlarını hatasız birbirine bağlar:

**Windows (PowerShell):**
```powershell
cmake --preset windows-debug ; cmake --build build ; ctest --test-dir build --output-on-failure ; .\build\main_app.exe
```

**Linux (Bash):**
```bash
cmake --preset linux-debug && cmake --build build && ctest --test-dir build --output-on-failure && ./build/main_app
```