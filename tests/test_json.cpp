#include <catch2/catch_test_macros.hpp>
#include "services/json_service.hpp"

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

    SECTION("JsonService tasima (move) semantigi dogru calismali")
    {
        JsonService moved_service = std::move(service);
        std::string raw = moved_service.serialize(config);
        auto parsed = moved_service.deserialize(raw);

        REQUIRE(parsed.has_value());
        CHECK(parsed->port == 9000);
    }

    SECTION("Turkce karakter ve JSON kacis sembolleri dogru ayristirilmali")
    {
        std::string_view unicode_json = R"({
            "host": "sunucu-öçşığü.yerel",
            "port": 8080,
            "active": true
        })";

        auto parsed = service.deserialize(unicode_json);
        REQUIRE(parsed.has_value());
        CHECK(parsed->host == "sunucu-öçşığü.yerel");
    }

    SECTION("Bozuk JSON syntax durumunda std::nullopt donmeli")
    {
        std::string_view corrupted_json = "{ host: 123, port: ";
        auto result = service.deserialize(corrupted_json);
        CHECK_FALSE(result.has_value());
    }

    SECTION("Bos girdi guvenle ele alinmali")
    {
        CHECK_FALSE(service.deserialize("").has_value());
        CHECK_FALSE(service.deserialize("   ").has_value());
    }

    SECTION("Eksik alan (missing fields) iceren JSON reddedilmeli")
    {
        std::string_view missing_fields = R"({"host": "127.0.0.1"})";
        auto result = service.deserialize(missing_fields);
        CHECK_FALSE(result.has_value());
    }

    SECTION("Tip uyusmazligi (type mismatch) durumunda exception sizdirilmamali")
    {
        std::string_view type_mismatch = R"({
            "host": "127.0.0.1",
            "port": "seksen_seksen",
            "active": true
        })";
        auto result = service.deserialize(type_mismatch);
        CHECK_FALSE(result.has_value());
    }

    SECTION("Ekstra / bilinmeyen alanlar toleransla karsilanmali")
    {
        std::string_view extra_fields = R"({
            "host": "10.0.0.1",
            "port": 443,
            "active": false,
            "extra_key": "ignored_value",
            "meta": [1, 2, 3]
        })";
        auto result = service.deserialize(extra_fields);
        REQUIRE(result.has_value());
        CHECK(result->host == "10.0.0.1");
        CHECK(result->port == 443);
        CHECK(result->active == false);
    }

    SECTION("Sinir port degerleri (0 ve 65535) korunmali")
    {
        AppConfig max_port_config{"0.0.0.0", 65535, false};
        std::string raw = service.serialize(max_port_config);
        auto parsed = service.deserialize(raw);

        REQUIRE(parsed.has_value());
        CHECK(parsed->port == 65535);

        AppConfig min_port_config{"0.0.0.0", 0, false};
        raw = service.serialize(min_port_config);
        parsed = service.deserialize(raw);

        REQUIRE(parsed.has_value());
        CHECK(parsed->port == 0);
    }
}