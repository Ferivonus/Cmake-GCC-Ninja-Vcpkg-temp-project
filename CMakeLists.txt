cmake_minimum_required(VERSION 3.25)
project(TemplateApp LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Catch2 3 REQUIRED)
find_package(boost_asio REQUIRED CONFIG)
find_package(boost_beast REQUIRED CONFIG)

add_library(json_service STATIC services/json_service.cpp)
target_compile_features(json_service PRIVATE cxx_std_17)
target_include_directories(json_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/services)
target_link_libraries(json_service PRIVATE nlohmann_json::nlohmann_json)

add_library(app_config STATIC config/app_config.cpp)
target_compile_features(app_config PRIVATE cxx_std_17)
target_include_directories(app_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/config)
target_link_libraries(app_config PRIVATE spdlog::spdlog)

add_executable(main_app main.cpp)
target_compile_features(main_app PRIVATE cxx_std_26)
target_compile_options(main_app PRIVATE 
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)
target_link_libraries(main_app PRIVATE 
    json_service 
    app_config
    Boost::beast
    Boost::asio
    Threads::Threads
    $<$<PLATFORM_ID:Windows>:ws2_32 mswsock>
)

enable_testing()
add_executable(unit_tests 
    tests/test_json.cpp
    tests/test_http.cpp
)
target_compile_features(unit_tests PRIVATE cxx_std_17)
target_compile_options(unit_tests PRIVATE 
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
)
target_link_libraries(unit_tests PRIVATE 
    json_service 
    Catch2::Catch2WithMain
    Boost::beast
    Boost::asio
    Threads::Threads
    $<$<PLATFORM_ID:Windows>:ws2_32 mswsock>
)

include(Catch)
catch_discover_tests(unit_tests)