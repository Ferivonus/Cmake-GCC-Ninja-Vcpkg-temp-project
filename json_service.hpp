#pragma once
#include <string>

struct AppConfig
{
    std::string host;
    int port;
    bool active;
};

std::string serialize_config(const AppConfig &cfg);
AppConfig deserialize_config(const std::string &raw_json);