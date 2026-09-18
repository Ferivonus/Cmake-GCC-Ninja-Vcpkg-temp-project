# Modern C++26 Cloud & Chat Sistemi

C++26 standardı, CMake, Ninja, GCC ve vcpkg (manifest modu) altyapısıyla geliştirilmiş platformlar arası (Windows / Linux) dağıtık bir sistem şablonudur. 

Bünyesinde **REST HTTP/1.1 API**, **tam çift yönlü (full-duplex) WebSocket chat sunucusu**, **SQLite3 mesaj kalıcılığı (WAL modu)**, **RFC 1928 Tor SOCKS5 vekil desteği**, **LTO (Link-Time Optimization)** ve **sembol temizleme (strip)** optimizasyonları, **nlohmann-json**, **spdlog** ve **Catch2 v3** birim test altyapısı barındırır.

---

## Gereksinimler

- **GCC (g++ 14+ / 15+):** C++26 derleme desteği
- **CMake 3.25+:** Modern hedef (`target_*`) mimarisi
- **Ninja:** Hızlı ve paralel inşa motoru
- **vcpkg:** Manifest modunda (`vcpkg.json`) C++ paket yönetimi
- **Git:** vcpkg ve modül bağımlılıkları için
- **Tor Servisi (İsteğe bağlı):** `.onion` ve anonim chat rotalaması için (Varsayılan SOCKS5: `127.0.0.1:9050` veya Tor Browser için `9150`)

---

## 1. Geliştirme Ortamı Kurulumu

### Windows
Gerekli MinGW ve geliştirme araçları kurulu değilse, **Yönetici olarak açılmış** bir PowerShell penceresinde ortam hazırlama betiğini çalıştırın:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\setup-cpp-env.ps1
```

### Linux (Ubuntu / Debian)
Gerekli derleme araçlarını kurun ve vcpkg ortam değişkenini tanımlayın:

```bash
sudo apt update && sudo apt install -y build-essential gcc-14 g++-14 cmake ninja-build git curl zip unzip tar pkg-config tor

# vcpkg kurulu olduğu dizini ortam değişkeni olarak ekleyin:
export VCPKG_ROOT="$HOME/vcpkg"
```

---

## 2. Otomasyon Betiği ile Yönetim (`build.ps1` - Windows)

Windows ortamında yapılandırma, derleme, test ve çalıştırma adımlarını hata kontrollü olarak tek komutla yönetmek için `build.ps1` betiğini kullanabilirsiniz:

```powershell
# Yalnızca Debug modunda derle ve birim testleri koştur
.\build.ps1

# Debug sunucusunu başlat (Varsayılan Port: 8080)
.\build.ps1 -Server

# Özel port ile sunucu başlat
.\build.ps1 -Server -Port 9000

# İstemci olarak bağlan
.\build.ps1 -Client Ahmet

# Tor SOCKS5 vekili üzerinden istemci başlat (Tor Browser: 9150)
.\build.ps1 -Client Anonim -Tor -ProxyPort 9150

# Release modunda (LTO + Strip) derle ve sunucuyu aç
.\build.ps1 release -Server

# Testleri atlayarak hızlıca başlat
.\build.ps1 -Server -NoTest

# Derleme dizinini temizleyip sıfırdan derle
.\build.ps1 -Clean -Server
```

---

## 3. Manuel Yapılandırma ve Derleme (CMake)

### A. Yapılandırma (Configure)

Projede `Debug` ve `Release` derlemeleri için ayrılmış bağımsız profiller yer alır:

**Windows (MinGW GCC):**
```powershell
cmake --preset windows-debug
cmake --preset windows-release
```

**Linux (GCC):**
```bash
cmake --preset linux-debug
cmake --preset linux-release
```

### B. Derleme (Build)

```bash
# Debug Derlemesi
cmake --build build/debug

# Release Derlemesi
cmake --build build/release
```

### C. Testleri Koşturma (CTest)

```bash
# Debug Testleri
ctest --test-dir build/debug --output-on-failure

# Release Testleri
ctest --test-dir build/release --output-on-failure
```

---

## 4. Hızlı Geliştirme Döngüsü (Tek Satır Manuel Komutlar)

### Windows (PowerShell)
```powershell
# Debug Modu (Yapılandır + Derle + Test + Çalıştır)
cmake --preset windows-debug ; cmake --build build/debug ; ctest --test-dir build/debug --output-on-failure ; .\build\debug\main_app.exe server 8080

# Release Modu (Yapılandır + Derle + Test + Çalıştır)
cmake --preset windows-release ; cmake --build build/release ; ctest --test-dir build/release --output-on-failure ; .\build\release\main_app.exe server 8080
```

### Linux (Bash)
```bash
# Debug Modu
cmake --preset linux-debug && cmake --build build/debug && ctest --test-dir build/debug --output-on-failure && ./build/debug/main_app server 8080

# Release Modu
cmake --preset linux-release && cmake --build build/release && ctest --test-dir build/release --output-on-failure && ./build/release/main_app server 8080
```

---

## 5. Çalıştırma ve Kullanım Senaryoları

Tek bir ikili dosya (`main_app` / `main_app.exe`) üzerinden hem sunucu hem de istemci modları başlatılabilir.

### A. Sunucu Modu (Server)

HTTP API ve WebSocket havuzunu başlatır, SQLite veritabanını (`app_chat.db`) WAL modunda açar:

**Windows (PowerShell):**
```powershell
# Varsayılan (Port: 8080, IP: 0.0.0.0)
.\build\release\main_app.exe server

# Özel port ve IP ile başlatma
.\build\release\main_app.exe server 8080 0.0.0.0
```

**Linux (Bash):**
```bash
# Varsayılan (Port: 8080, IP: 0.0.0.0)
./build/release/main_app server

# Özel port ve IP ile başlatma
./build/release/main_app server 8080 0.0.0.0
```

---

### B. İstemci Modu (Client)

Terminal tabanlı interaktif sohbet istemcisi:

**Windows (PowerShell):**
```powershell
# 1. Doğrudan Yerel Bağlantı
.\build\release\main_app.exe client Ahmet 127.0.0.1 8080

# 2. İkinci Kullanıcı ile Canlı Sohbet
.\build\release\main_app.exe client Mehmet 127.0.0.1 8080

# 3. Tor SOCKS5 Vekili Üzerinden Bağlantı (Varsayılan port 9050)
.\build\release\main_app.exe client Anonim 127.0.0.1 8080 --tor

# 4. Tor Browser Üzerinden Yönlendirme (Port 9150)
.\build\release\main_app.exe client Anonim 127.0.0.1 8080 --proxy-port 9150

# 5. Otomatik Tor Rotalaması (.onion Gizli Servisler)
.\build\release\main_app.exe client Anonim chatroomxxxxxxxx.onion 8080

# 6. Harici Tor Vekil Makinesi Belirtme
.\build\release\main_app.exe client Anonim chatroomxxxxxxxx.onion 8080 --proxy-host 192.168.1.50 --proxy-port 9050
```

**Linux (Bash):**
```bash
# 1. Doğrudan Yerel Bağlantı
./build/release/main_app client Ahmet 127.0.0.1 8080

# 2. İkinci Kullanıcı ile Canlı Sohbet
./build/release/main_app client Mehmet 127.0.0.1 8080

# 3. Tor SOCKS5 Vekili Üzerinden Bağlantı (Varsayılan port 9050)
./build/release/main_app client Anonim 127.0.0.1 8080 --tor

# 4. Tor Browser Üzerinden Yönlendirme (Port 9150)
./build/release/main_app client Anonim 127.0.0.1 8080 --proxy-port 9150

# 5. Otomatik Tor Rotalaması (.onion Gizli Servisler)
./build/release/main_app client Anonim chatroomxxxxxxxx.onion 8080

# 6. Harici Tor Vekil Makinesi Belirtme
./build/release/main_app client Anonim chatroomxxxxxxxx.onion 8080 --proxy-host 192.168.1.50 --proxy-port 9050
```

---

## 6. HTTP API Testleri (cURL Örnekleri)

Sunucu çalışırken yapılandırma durumunu REST üzerinden denetleyin:

### Windows (PowerShell)
```powershell
# Durumu Oku (GET)
curl.exe [http://127.0.0.1:8080/api/config](http://127.0.0.1:8080/api/config)

# Ayarları Güncelle (POST)
curl.exe -X POST [http://127.0.0.1:8080/api/config](http://127.0.0.1:8080/api/config) -H "Content-Type: application/json" -d "{\"host\":\"node1.internal\",\"port\":9000,\"active\":true}"

# Ayarları Sıfırla (DELETE)
curl.exe -X DELETE [http://127.0.0.1:8080/api/config](http://127.0.0.1:8080/api/config)
```

### Linux (Bash)
```bash
# Durumu Oku (GET)
curl [http://127.0.0.1:8080/api/config](http://127.0.0.1:8080/api/config)

# Ayarları Güncelle (POST)
curl -X POST [http://127.0.0.1:8080/api/config](http://127.0.0.1:8080/api/config) -H "Content-Type: application/json" -d '{"host":"node1.internal","port":9000,"active":true}'

# Ayarları Sıfırla (DELETE)
curl -X DELETE [http://127.0.0.1:8080/api/config](http://127.0.0.1:8080/api/config)
```

---

## 7. Proje Mimarisi

```text
├── backend/
│   ├── backend.cpp / .hpp        # Boost.Asio Acceptor ve oturum orkestrasyonu
│   ├── http_handler.cpp / .hpp   # REST API CRUD yönlendiricisi
│   ├── ws_handler.cpp / .hpp     # WebSocket el sıkışma ve broadcast motoru
│   └── server_state.hpp          # Oturum havuzu, paylaşımlı mutex ve durum yönetimi
├── client/
│   ├── ws_client.cpp / .hpp      # Çift yönlü WS istemcisi ve RFC 1928 Tor SOCKS5 katmanı
├── config/
│   └── app_config.cpp / .hpp     # spdlog logger ve genel yapılandırma yapıları
├── services/
│   ├── db_service.cpp / .hpp     # SQLite3 WAL modu, thread-safe sorgu motoru
│   └── json_service.cpp / .hpp   # nlohmann-json serileştirme soyutlaması
├── tests/
│   ├── test_db.cpp               # SQLite in-memory CRUD & Injection birim testleri
│   ├── test_http.cpp             # API routing, status code & Beast birim testleri
│   └── test_json.cpp             # JSON ayrıştırma & UTF-8 sınır testleri
├── build.ps1                     # Otomatik derleme, test ve başlatma betiği
├── CMakeLists.txt                # C++26 hedef tabanlı modüler derleme kuralları
├── CMakePresets.json             # MinGW / Linux derleme profilleri (Debug & Release)
└── vcpkg.json                    # Bağımlılık manifestosu
```