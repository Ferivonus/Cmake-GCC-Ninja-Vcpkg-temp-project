#include <iostream>
#include <utility>
#include "json_service.hpp"

int main()
{
    AppConfig config{"127.0.0.1", 8080, true};

    std::string json_text = serialize_config(config);
    std::cout << "JSON Ciktisi:\n"
              << json_text << "\n";

    auto [host_val, port_val] = std::pair{config.host, config.port};
    std::cout << "Host: " << host_val << ", Port: " << port_val << "\n";

    return 0;
}