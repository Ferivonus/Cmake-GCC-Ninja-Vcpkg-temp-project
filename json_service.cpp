#include "json_service.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string serialize_config(const AppConfig &cfg)
{
    json j = {
        {"host", cfg.host},
        {"port", cfg.port},
        {"active", cfg.active}};
    return j.dump(2);
}

AppConfig deserialize_config(const std::string &raw_json)
{
    auto j = json::parse(raw_json);
    return AppConfig{
        j["host"].get<std::string>(),
        j["port"].get<int>(),
        j["active"].get<bool>()};
}