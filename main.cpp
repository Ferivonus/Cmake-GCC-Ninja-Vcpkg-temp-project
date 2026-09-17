#include <iostream>

#include "services/json_service.hpp"
#include "config/app_config.hpp"

int main()
{
    // spdlog katmanını başlat
    AppLog::init();
    AppLog::info("Uygulama baslatildi.");

    JsonService service;
    AppConfig config{"127.0.0.1", 8080, true};

    std::string json_text = service.serialize(config);
    std::cout << "JSON Çıktısı:\n"
              << json_text << "\n";

    auto [host, port, _] = config;
    AppLog::info("Baglanti ayari okundu: " + host + ":" + std::to_string(port));

    if (auto parsed = service.deserialize(json_text))
    {
        AppLog::info("Ayrıştırma basarili: " + parsed->host);
    }
    else
    {
        AppLog::error("JSON ayristirma basarisiz oldu!");
    }

    return 0;
}