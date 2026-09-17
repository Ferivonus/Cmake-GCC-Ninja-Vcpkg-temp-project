cmake_minimum_required(VERSION 3.25)
project(test LANGUAGES CXX)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(json_service STATIC json_service.cpp)
target_compile_features(json_service PRIVATE cxx_std_17)
target_link_libraries(json_service PRIVATE nlohmann_json::nlohmann_json)

add_executable(test main.cpp)
target_compile_features(test PRIVATE cxx_std_26)

# 3. İkisini birbirine bağla
target_link_libraries(test PRIVATE json_service)