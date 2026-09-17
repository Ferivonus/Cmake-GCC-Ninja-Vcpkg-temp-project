#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif
#include "json_service.hpp"

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    JsonService service;
    AppConfig config{"127.0.0.1", 8080, true};

    std::string json_text = service.serialize(config);
    std::cout << "JSON Çıktısı:\n"
              << json_text << "\n";

    auto [host, port, _] = config;
    std::cout << "Host: " << host << ", Port: " << port << "\n";

    if (auto parsed = service.deserialize(json_text))
    {
        std::cout << "Ayrıştırma başarılı: " << parsed->host << "\n";
    }

    return 0;
}