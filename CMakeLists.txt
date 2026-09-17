cmake_minimum_required(VERSION 3.25)
project(test LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 3 REQUIRED)

# 1. JSON Servis Katmanı (C++17)
add_library(json_service STATIC services/json_service.cpp)
target_compile_features(json_service PRIVATE cxx_std_17)
target_include_directories(json_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/services)
target_link_libraries(json_service PRIVATE nlohmann_json::nlohmann_json)

# 2. Log Katmanı (C++11)
add_library(app_config STATIC config/app_config.cpp)
target_compile_features(app_config PRIVATE cxx_std_11)
target_include_directories(app_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/config)
target_link_libraries(app_config PRIVATE spdlog::spdlog)

add_executable(main_app main.cpp)
target_compile_features(main_app PRIVATE cxx_std_26)
target_compile_options(main_app PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>)
target_link_libraries(main_app PRIVATE json_service app_config)
set_target_properties(main_app PROPERTIES OUTPUT_NAME "main_app")

# 4. Birim Testler -> Diskte 'unit_tests.exe' üretir
enable_testing()
add_executable(unit_tests tests/test_json.cpp)
target_compile_features(unit_tests PRIVATE cxx_std_17)
target_link_libraries(unit_tests PRIVATE json_service Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(unit_tests)