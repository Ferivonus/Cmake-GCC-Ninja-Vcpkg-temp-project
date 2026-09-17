# CMake ile C++26 Test Projesi

Bu proje; CMake, Ninja, GCC ve vcpkg entegrasyonuyla modern C++ (C++26) uygulamaları geliştirmek için hazırlanmış temel bir proje şablonudur.

## Gereksinimler

- **GCC (g++ 15+):** C++26 standart desteği için
- **CMake 3.25+:** Modern hedef tabanlı yapılandırma ve C++26 desteği için
- **Ninja:** Hızlı ve paralel inşa motoru
- **vcpkg:** Manifest modunda (`vcpkg.json`) paket ve bağımlılık yönetimi için
- **Git:** vcpkg kurulumu ve paket indirmeleri için

---

## 1. Geliştirme Ortamı Kurulumu

Gerekli araçlar sisteminizde kurulu değilse, **Yönetici olarak açılmış** bir PowerShell penceresinde ortam hazırlama betiğini çalıştırın:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\setup-cpp-env.ps1
```

---

## 2. Yapılandırma (Configure)

Ninja oluşturucusunu ve vcpkg bağımlılık zincirini tetikleyerek derleme dosyalarını hazırlar:

```powershell
cmake --preset gcc-ninja-debug
```

---

## 3. Derleme (Build)

Projeyi paralel olarak derler:

```powershell
cmake --build build
```

---

## 4. Çalıştırma (Run)

Üretilen çalıştırılabilir dosyayı başlatır:

```powershell
.\build\test.exe
```