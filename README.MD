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

# Debug sunucusunu başlat (Varsayılan Port: 8080, Dinlenen IP: 0.0.0.0)
.\build.ps1 -Server

# Özel port ve özel dinleme IP'si ile sunucu başlat
.\build.ps1 -Server -Port 9000 -BindAddress 127.0.0.1

# İstemci olarak bağlan (hedef varsayılan: 127.0.0.1)
.\build.ps1 -Client Ahmet

# Belirli bir hedefe (IP/domain) bağlan
.\build.ps1 -Client Ahmet -Target 192.168.1.20 -Port 8080

# Bir .onion gizli servisine bağlan (Tor otomatik devreye girer)
.\build.ps1 -Client Anonim -Target chatroomxxxxxxxx.onion -ProxyPort 9150

# Tor SOCKS5 vekili üzerinden düz bir IP'ye bağlan (Tor Browser: 9150)
.\build.ps1 -Client Anonim -Target 127.0.0.1 -Tor -ProxyPort 9150

# Release modunda (LTO + Strip) derle ve sunucuyu aç
.\build.ps1 release -Server

# Testleri atlayarak hızlıca başlat
.\build.ps1 -Server -NoTest

# Derleme dizinini temizleyip sıfırdan derle
.\build.ps1 -Clean -Server

# Zaten build alınmış exe'yi, yeniden build almadan direkt çalıştır
# (-Server/-Client ile çağrıldığında exe zaten varsa ZATEN OTOMATİK olarak
#  yeniden build alınmaz; -SkipBuild sadece bunu elle zorlamak içindir)
.\build.ps1 -SkipBuild -Server
.\build.ps1 -SkipBuild -Client Ahmet -Target 192.168.1.20

# Exe zaten var olsa bile zorla yeniden build al
.\build.ps1 -Rebuild -Server
```

> **Not:** `-BindAddress` yalnızca **sunucu** modunda (dinlenecek IP) kullanılır; `-Target` ise yalnızca **istemci** modunda (bağlanılacak IP/domain/`.onion` adresi) kullanılır. Eski `-HostAddress` ismi geriye dönük uyumluluk için `-Target` parametresine alias olarak bağlıdır.
>
> **Not:** `-Server` veya `-Client` ile çağrıldığında, `main_app.exe` zaten build edilmişse betik **otomatik olarak** hiçbir configure/build/test adımına dokunmaz ve doğrudan çalıştırır — bu sayede örneğin sunucu açıkken başka bir pencerede istemciyi başlatmak, çalışan sunucuyu kapatmaz. Exe henüz yoksa otomatik olarak önce build alınır. Zorla yeniden build almak için `-Rebuild`, build'e kesinlikle dokunmamak (exe yoksa hata verir) için `-SkipBuild` kullanılır.

> **Not:** `build.ps1`, tek sorumluluk ilkesiyle yalnızca derleme/test/çalıştırma işleriyle ilgilenir. Çalışan bir sunucudaki **odaları** ve **REST API yapılandırmasını** (`/api/config`) yönetmek için tamamen bağımsız bir betik olan **`controller.ps1`** kullanılır — bkz. Bölüm 3. Bu sayede aynı REST çağrıları iki farklı dosyada tekrar tanımlanmaz.

---

## 3. Sunucu Yönetimi (`controller.ps1`) — Postman'a Gerek Yok

Çalışan bir sunucudaki **odaları** ve **REST API yapılandırmasını** yönetmenin tek yolu `controller.ps1` betiğidir. Bu betik `build.ps1`'den tamamen bağımsızdır, derleme/test adımlarına hiç dokunmaz; yalnızca hedef sunucuya HTTP isteği atar. Postman veya `curl` yerine doğrudan PowerShell üzerinden aynı istekleri hazır komutlarla göndermenizi sağlar.

Varsayılan hedef `127.0.0.1:8080`'dir; farklı bir sunucu/port için her komuta `-Target` ve `-Port` eklenebilir. Betiğin tüm parametreleri için `Get-Help .\controller.ps1 -Full` komutunu kullanabilirsiniz.

### 3.1 Oda (Room) Komutları

```powershell
# Sunucudaki tüm odaları listele
.\controller.ps1 -ListRooms

# Farklı bir sunucu/port üzerindeki odaları listele
.\controller.ps1 -ListRooms -Target 192.168.1.20 -Port 8080

# Yeni bir oda oluştur
.\controller.ps1 -CreateRoom "Genel Sohbet"

# Bir odayı kapat (yeni bağlantılara kapatır, ID veya oda adı ile)
.\controller.ps1 -CloseRoom "Genel Sohbet"
.\controller.ps1 -CloseRoom 3

# Kapalı bir odayı yeniden aç
.\controller.ps1 -OpenRoom 3

# Bir odayı ve tüm mesaj geçmişini kalıcı olarak sil
.\controller.ps1 -DeleteRoom 3
```

> **Not:** `-CloseRoom` / `-OpenRoom` / `-DeleteRoom` parametrelerine sayısal bir oda ID'si veya oda adı (string) verilebilir; isim verildiğinde betik önce `GET /api/rooms` ile ID'yi otomatik çözümler. Bu komutlar için **hedef sunucunun çalışıyor olması** gerekir. Sunucu tarafı (`GET/POST/PUT/DELETE /api/rooms`) `http_handler.cpp` içinde karşılanmalıdır.

### 3.2 Yapılandırma (Config) Komutları

```powershell
# Mevcut yapılandırmayı oku
.\controller.ps1 -GetConfig

# Yapılandırmayı güncelle
.\controller.ps1 -SetConfig -ConfigHost "node1.internal" -ConfigPort 9000 -ConfigActive $true

# Yapılandırmayı sıfırla
.\controller.ps1 -ResetConfig
```

### 3.3 Komutsuz Çalıştırma

Herhangi bir parametre vermeden `.\controller.ps1` çalıştırıldığında, betik yukarıdaki tüm komutları örnekleriyle birlikte ekrana yazdırır — yani kullanılabilir komutları hatırlamak için betiğin kendisine bakmanız yeterlidir.

---

## 4. Manuel Yapılandırma ve Derleme (CMake)

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

## 5. Hızlı Geliştirme Döngüsü (Tek Satır Manuel Komutlar)

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

## 6. Çalıştırma ve Kullanım Senaryoları

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

## 7. HTTP API Testleri (cURL Örnekleri)

Sunucu çalışırken yapılandırma durumunu REST üzerinden denetleyin. Aşağıdaki komutların Windows'ta PowerShell ile çalıştırılabilen hazır karşılıkları için Bölüm 3.2'deki `controller.ps1 -GetConfig / -SetConfig / -ResetConfig` komutlarını kullanabilirsiniz:

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

### Oda Yönetimi (`/api/rooms`)

`controller.ps1 -ListRooms / -CreateRoom / -CloseRoom / -OpenRoom / -DeleteRoom` (bkz. Bölüm 3.1) komutları perde arkasında bu uç noktaları kullanır. Aşağıdaki `curl` örnekleri, aynı isteklerin ham HTTP karşılığıdır ve referans/hata ayıklama amaçlıdır:

**Windows (PowerShell)**
```powershell
# Odaları Listele (GET)
curl.exe [http://127.0.0.1:8080/api/rooms](http://127.0.0.1:8080/api/rooms)

# Yeni Oda Olustur (POST)
curl.exe -X POST [http://127.0.0.1:8080/api/rooms](http://127.0.0.1:8080/api/rooms) -H "Content-Type: application/json" -d "{\"name\":\"Genel Sohbet\"}"

# Odayi Kapat/Ac (PUT)
curl.exe -X PUT [http://127.0.0.1:8080/api/rooms/3](http://127.0.0.1:8080/api/rooms/3) -H "Content-Type: application/json" -d "{\"is_open\":false}"

# Odayi Sil (DELETE)
curl.exe -X DELETE [http://127.0.0.1:8080/api/rooms/3](http://127.0.0.1:8080/api/rooms/3)
```

**Linux (Bash)**
```bash
# Odalari Listele (GET)
curl [http://127.0.0.1:8080/api/rooms](http://127.0.0.1:8080/api/rooms)

# Yeni Oda Olustur (POST)
curl -X POST [http://127.0.0.1:8080/api/rooms](http://127.0.0.1:8080/api/rooms) -H "Content-Type: application/json" -d '{"name":"Genel Sohbet"}'

# Odayi Kapat/Ac (PUT)
curl -X PUT [http://127.0.0.1:8080/api/rooms/3](http://127.0.0.1:8080/api/rooms/3) -H "Content-Type: application/json" -d '{"is_open":false}'

# Odayi Sil (DELETE)
curl -X DELETE [http://127.0.0.1:8080/api/rooms/3](http://127.0.0.1:8080/api/rooms/3)
```

---

## 8. Proje Mimarisi

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
├── controller.ps1                # REST API yönetim betiği (oda + config, Postman gerektirmez)
├── CMakeLists.txt                # C++26 hedef tabanlı modüler derleme kuralları
├── CMakePresets.json             # MinGW / Linux derleme profilleri (Debug & Release)
└── vcpkg.json                    # Bağımlılık manifestosu
```