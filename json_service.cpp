#include "json_service.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct JsonService::Impl
{
    std::string serialize(const AppConfig &cfg) const
    {
        json j = {
            {"host", cfg.host},
            {"port", cfg.port},
            {"active", cfg.active}};
        return j.dump(2);
    }

    std::optional<AppConfig> deserialize(std::string_view raw_json) const
    {
        try
        {
            auto j = json::parse(raw_json);
            return AppConfig{
                j.at("host").get<std::string>(),
                j.at("port").get<int>(),
                j.at("active").get<bool>()};
        }
        catch (const json::exception &)
        {
            return std::nullopt;
        }
    }
};

JsonService::JsonService() : m_impl(std::make_unique<Impl>()) {}
JsonService::~JsonService() = default;

JsonService::JsonService(JsonService &&) noexcept = default;
JsonService &JsonService::operator=(JsonService &&) noexcept = default;

std::string JsonService::serialize(const AppConfig &cfg) const
{
    return m_impl->serialize(cfg);
}

std::optional<AppConfig> JsonService::deserialize(std::string_view raw_json) const
{
    return m_impl->deserialize(raw_json);
}