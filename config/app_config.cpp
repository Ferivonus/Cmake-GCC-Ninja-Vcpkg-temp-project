#include "app_config.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace AppLog
{
    void init()
    {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
#endif
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_level(spdlog::level::debug);
    }

    void info(std::string_view msg)
    {
        spdlog::info("{}", msg);
    }

    void warn(std::string_view msg)
    {
        spdlog::warn("{}", msg);
    }

    void error(std::string_view msg)
    {
        spdlog::error("{}", msg);
    }
}