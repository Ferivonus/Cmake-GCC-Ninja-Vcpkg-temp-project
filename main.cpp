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

    std::printf("JSON Çıktısı (printf):\n%s\n", json_text.c_str());

    auto [host, port, _] = config;

    std::printf("Host: %s, Port: %d\n", host.c_str(), port);

    if (auto parsed = service.deserialize(json_text))
    {
        std::cout << "Ayrıştırma başarılı: " << parsed->host << "\n";
    }

    return 0;
}