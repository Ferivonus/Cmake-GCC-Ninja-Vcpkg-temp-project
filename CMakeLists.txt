cmake_minimum_required(VERSION 3.30)
project(TemplateApp LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# LTO / IPO Kontrolü
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Gerekli Paketlerin Taranması
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 3 REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${Catch2_DIR})

find_package(boost_asio REQUIRED CONFIG)
find_package(boost_beast REQUIRED CONFIG)
find_package(SQLite3 REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

# 1. Json Service
add_library(json_service STATIC services/json_service.cpp)
target_compile_features(json_service PRIVATE cxx_std_26)
target_include_directories(json_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(json_service PRIVATE nlohmann_json::nlohmann_json)

# 2. App Config
add_library(app_config STATIC config/app_config.cpp)
target_compile_features(app_config PRIVATE cxx_std_26)
target_include_directories(app_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(app_config PRIVATE spdlog::spdlog)

# 3. Crypto Service (AES-256-GCM ve PBKDF2 Katmanı)
add_library(crypto_service STATIC services/crypto_service.cpp)
target_compile_features(crypto_service PRIVATE cxx_std_26)
target_include_directories(crypto_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(crypto_service PUBLIC 
    OpenSSL::Crypto
    $<$<PLATFORM_ID:Windows>:crypt32>
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

# 4. Backend Service
add_library(backend_service STATIC 
    services/db_service.cpp
    backend/backend.cpp
    backend/http_handler.cpp
    backend/ws_handler.cpp
)
target_compile_features(backend_service PRIVATE cxx_std_26)
target_include_directories(backend_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(backend_service PUBLIC
    json_service
    app_config
    crypto_service
    Boost::beast
    Boost::asio
    Threads::Threads
    SQLite::SQLite3
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:mswsock>
)

# 5. Client Service (E2EE Kripto ile Güçlendirilmiş İstemci)
add_library(client_service STATIC
    client/ws_client.cpp
)
target_compile_features(client_service PRIVATE cxx_std_26)
target_include_directories(client_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(client_service PUBLIC
    app_config
    crypto_service
    Boost::beast
    Boost::asio
    Threads::Threads
    nlohmann_json::nlohmann_json
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:mswsock>
)

# 6. Main Uygulama (Sunucu ve İstemci Giriş Noktası)
add_executable(main_app main.cpp)
target_compile_features(main_app PRIVATE cxx_std_26)
target_compile_options(main_app PRIVATE 
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)
target_link_options(main_app PRIVATE
    $<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU>>:-s>
)
target_include_directories(main_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(main_app PRIVATE 
    backend_service
    client_service
    CLI11::CLI11
)

# 7. Birim Testleri (Crypto, DB, HTTP ve JSON Kapsamı)
enable_testing()
add_executable(unit_tests 
    tests/test_json.cpp
    tests/test_http.cpp
    tests/test_db.cpp
    tests/test_crypto.cpp
)
target_compile_features(unit_tests PRIVATE cxx_std_26)
target_compile_options(unit_tests PRIVATE 
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(unit_tests PRIVATE 
    backend_service
    crypto_service
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(unit_tests)