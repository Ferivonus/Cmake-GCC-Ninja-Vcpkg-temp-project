# CMake ile C++ Test Projesi
Bu proje, CMake kullanarak bir C++ test projesi oluşturmak için temel bir yapı

# Gereksinimler
- CMake 3.25 veya daha yeni bir sürüm
- Ninja build sistemi
- vcpkg paket yöneticisi (isteğe bağlı, bağımlılık yönetimi için)

# Yapılandırma (Ninja ve vcpkg tetiklenir)
cmake --preset gcc-ninja-debug

# Derleme
cmake --build build

# Çalıştırma
.\build\test.exe

