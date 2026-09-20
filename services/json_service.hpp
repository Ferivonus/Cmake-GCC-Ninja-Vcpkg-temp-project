#pragma once
#include "config/app_config.hpp"
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class JsonService
{
public:
    JsonService();
    ~JsonService();

    JsonService(JsonService &&) noexcept;
    JsonService &operator=(JsonService &&) noexcept;

    JsonService(const JsonService &) = delete;
    JsonService &operator=(const JsonService &) = delete;

    [[nodiscard]] std::string serialize(const AppConfig &cfg) const;
    [[nodiscard]] std::optional<AppConfig> deserialize(std::string_view raw_json) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};