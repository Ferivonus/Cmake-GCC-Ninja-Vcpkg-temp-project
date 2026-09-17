cmake_minimum_required(VERSION 3.25)
project(test LANGUAGES CXX)

# IntelliSense ve Clangd için derleme veritabanı oluşturur
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json CONFIG REQUIRED)

# 1. JSON Servis Katmanı (C++17)
add_library(json_service STATIC json_service.cpp)
target_compile_features(json_service PRIVATE cxx_std_17)
target_include_directories(json_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(json_service PRIVATE nlohmann_json::nlohmann_json)

# 2. Ana Uygulama (C++26)
add_executable(test main.cpp)
target_compile_features(test PRIVATE cxx_std_26)
target_compile_options(test PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>)

# 3. Bağlama
target_link_libraries(test PRIVATE json_service)