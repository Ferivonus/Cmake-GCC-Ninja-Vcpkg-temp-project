#pragma once
#include <string>
#include <string_view>

// Tüm sistem bileşenlerinin kullandığı temel yapılandırma modeli
struct AppConfig
{
    std::string host{"127.0.0.1"};
    int port{8080};
    bool active{true};
};

namespace AppLog
{
    void init();
    void info(std::string_view msg);
    void warn(std::string_view msg);
    void error(std::string_view msg);
}