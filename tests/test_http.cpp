#include <catch2/catch_test_macros.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include "backend/http_handler.hpp"
#include "backend/server_state.hpp"
#include "services/crypto_service.hpp"
#include <string>
#include <memory>

namespace http = boost::beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

static std::shared_ptr<backend::ServerState> create_test_state()
{
    auto state = std::make_shared<backend::ServerState>();
    state->config = AppConfig{"127.0.0.1", 8080, true};
    state->db_service.init(":memory:");
    return state;
}

TEST_CASE("Cloud API HTTP Handler - Saglik, CORS ve Konfigurasyon Endpoint'leri", "[http][api]")
{
    auto state = create_test_state();

    SECTION("OPTIONS on-kontrol (CORS Preflight) istegi 204 No Content ve uygun basliklar donmeli")
    {
        http::request<http::string_body> req{http::verb::options, "/api/rooms", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::no_content);
        CHECK(res[http::field::access_control_allow_origin] == "*");
        CHECK(res.find(http::field::access_control_allow_methods) != res.end());
    }

    SECTION("GET /health basarili sekilde 200 OK ve 'healthy' durum donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/health", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        auto body = json::parse(res.body());
        CHECK(body["status"] == "healthy");
        CHECK(body["database"] == "connected");
        CHECK(body.contains("uptime_seconds"));
        CHECK(res[http::field::access_control_allow_origin] == "*");
    }

    SECTION("POST /health durumunda 405 Method Not Allowed donmeli")
    {
        http::request<http::string_body> req{http::verb::post, "/health", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::method_not_allowed);
    }

    SECTION("GET /api/config mevcut yapilandirmayi 200 OK ile donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/api/config", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        auto body_json = json::parse(res.body());
        CHECK(body_json["host"] == "127.0.0.1");
        CHECK(body_json["port"] == 8080);
        CHECK(body_json["active"] == true);
    }

    SECTION("POST /api/config gecerli JSON ile guncelleme yapmali")
    {
        http::request<http::string_body> req{http::verb::post, "/api/config", 11};
        req.body() = R"({"host":"192.168.1.50","port":9090,"active":false})";
        req.prepare_payload();

        auto res = backend::handle_http_request(std::move(req), state);
        CHECK(res.result() == http::status::ok);

        std::lock_guard<std::mutex> lock(state->mtx);
        CHECK(state->config.host == "192.168.1.50");
        CHECK(state->config.port == 9090);
        CHECK(state->config.active == false);
    }

    SECTION("DELETE /api/config ayarlari varsayilana sifirlamali")
    {
        http::request<http::string_body> req{http::verb::delete_, "/api/config", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);

        std::lock_guard<std::mutex> lock(state->mtx);
        CHECK(state->config.active == false);
    }
}

TEST_CASE("Cloud API HTTP Handler - Oda ve Kriptolu Mesaj REST Endpoint'leri", "[http][rooms]")
{
    auto state = create_test_state();

    SECTION("GET /api/rooms baslangicta bos liste donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/api/rooms", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        auto body = json::parse(res.body());
        CHECK(body["rooms"].is_array());
        CHECK(body["rooms"].empty());
    }

    SECTION("POST /api/rooms gecerli isimle 201 Created donmeli")
    {
        http::request<http::string_body> req{http::verb::post, "/api/rooms", 11};
        req.body() = R"({"name":"Oyun Odasi"})";
        req.prepare_payload();

        auto res = backend::handle_http_request(std::move(req), state);
        CHECK(res.result() == http::status::created);

        auto body = json::parse(res.body());
        CHECK(body["status"] == "success");
        CHECK(body["room_id"] == 1);
        CHECK(body["name"] == "Oyun Odasi");
    }

    SECTION("POST /api/rooms ayni isimle tekrarlandiginda 409 Conflict donmeli")
    {
        http::request<http::string_body> req1{http::verb::post, "/api/rooms", 11};
        req1.body() = R"({"name":"Tekil Oda"})";
        req1.prepare_payload();
        backend::handle_http_request(std::move(req1), state);

        http::request<http::string_body> req2{http::verb::post, "/api/rooms", 11};
        req2.body() = R"({"name":"Tekil Oda"})";
        req2.prepare_payload();
        auto res = backend::handle_http_request(std::move(req2), state);

        CHECK(res.result() == http::status::conflict);
    }

    SECTION("GET, PUT, DELETE /api/rooms/{id} rotalari dogru calismali")
    {
        int64_t room_id = state->db_service.create_room("Test Odasi", "2026-09-19 12:00:00");
        std::string room_path = "/api/rooms/" + std::to_string(room_id);

        // 1. GET /api/rooms/{id}
        {
            http::request<http::string_body> req{http::verb::get, room_path, 11};
            auto res = backend::handle_http_request(std::move(req), state);
            CHECK(res.result() == http::status::ok);

            auto body = json::parse(res.body());
            CHECK(body["id"] == room_id);
            CHECK(body["name"] == "Test Odasi");
            CHECK(body["is_open"] == true);
        }

        // 2. PUT /api/rooms/{id}
        {
            http::request<http::string_body> req{http::verb::put, room_path, 11};
            req.body() = R"({"name":"Guncel Oda","is_open":false})";
            req.prepare_payload();

            auto res = backend::handle_http_request(std::move(req), state);
            CHECK(res.result() == http::status::ok);

            auto updated = state->db_service.get_room(room_id);
            REQUIRE(updated.has_value());
            CHECK(updated->name == "Guncel Oda");
            CHECK(updated->is_open == false);
        }

        // 3. DELETE /api/rooms/{id}
        {
            http::request<http::string_body> req{http::verb::delete_, room_path, 11};
            auto res = backend::handle_http_request(std::move(req), state);
            CHECK(res.result() == http::status::ok);

            CHECK_FALSE(state->db_service.get_room(room_id).has_value());
        }

        // 4. Silinmis odaya GET yapildiginda 404 Not Found donmeli
        {
            http::request<http::string_body> req{http::verb::get, room_path, 11};
            auto res = backend::handle_http_request(std::move(req), state);
            CHECK(res.result() == http::status::not_found);
        }
    }

    SECTION("GET /api/rooms/{id}/messages sifreli kaydedilmis mesajlari seffaf sekilde cozerek sunmali")
    {
        int64_t room_id = state->db_service.create_room("Kripto Odasi", "2026-09-19 12:00:00");
        const std::string original_secret = "Bu cok gizli bir iletidir!";

        // Mesajı AES-256-GCM ile şifreleyip zarf olarak kaydediyoruz
        auto enc = crypto::CryptoService::encrypt(original_secret, state->db_cipher_key);
        REQUIRE(enc.has_value());

        json enc_envelope = {
            {"__enc", true},
            {"c", enc->ciphertext_base64},
            {"iv", enc->iv_base64},
            {"tag", enc->tag_base64}};

        state->db_service.save_message(room_id, "Alice", enc_envelope.dump(), "2026-09-19 12:00:01");

        // REST API üzerinden mesaj geçmişi çağrıldığında
        http::request<http::string_body> req{http::verb::get, "/api/rooms/" + std::to_string(room_id) + "/messages", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        auto body = json::parse(res.body());
        REQUIRE(body["messages"].size() == 1);
        CHECK(body["messages"][0]["username"] == "Alice");
        // İstemciye dönen mesaj şifreli zarf DEĞİL, deşifre edilmiş açık metin olmalı
        CHECK(body["messages"][0]["message"] == original_secret);
    }

    SECTION("Sayisal formati bozan overflow ID durumunda 400 Bad Request donmeli")
    {
        // 64-bit tamsayı sınırını aşan bir ID
        http::request<http::string_body> req{http::verb::get, "/api/rooms/99999999999999999999999999999999/messages", 11};
        auto res = backend::handle_http_request(std::move(req), state);
        CHECK(res.result() == http::status::bad_request);
    }
}