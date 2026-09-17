#include <catch2/catch_test_macros.hpp>
#include "json_service.hpp"

TEST_CASE("JsonService serilestirme ve ayristirma dogrulamasi", "[json]")
{
    JsonService service;
    AppConfig config{"localhost", 9000, true};

    SECTION("Gecerli veri basariyla serilestirilip geri okunabilmeli")
    {
        std::string raw = service.serialize(config);
        REQUIRE_FALSE(raw.empty());

        auto parsed = service.deserialize(raw);
        REQUIRE(parsed.has_value());
        CHECK(parsed->host == "localhost");
        CHECK(parsed->port == 9000);
        CHECK(parsed->active == true);
    }

    SECTION("Bozuk JSON verisi std::nullopt donmeli (guvenli hata yonetimi)")
    {
        std::string_view corrupted_json = "{ host: 123, port: ";
        auto result = service.deserialize(corrupted_json);
        CHECK_FALSE(result.has_value());
    }
}