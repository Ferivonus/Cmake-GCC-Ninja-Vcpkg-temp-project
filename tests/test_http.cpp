#include <catch2/catch_test_macros.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include "backend/http_handler.hpp"
#include "backend/server_state.hpp"
#include <string>
#include <memory>

namespace http = boost::beast::http;
namespace asio = boost::asio;
using json = nlohmann::json;

// Test ortamı için bellek içi SQLite ile izole bir ServerState hazırlar
static std::shared_ptr<backend::ServerState> create_test_state()
{
    auto state = std::make_shared<backend::ServerState>();
    state->config = AppConfig{"127.0.0.1", 8080, true};
    state->db_service.init(":memory:");
    return state;
}

TEST_CASE("Cloud API HTTP Handler - Saglik ve Konfigurasyon Endpoint'leri", "[http][api]")
{
    auto state = create_test_state();

    SECTION("GET /health basarili sekilde 200 OK ve 'healthy' durum donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/health", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        auto body = json::parse(res.body());
        CHECK(body["status"] == "healthy");
        CHECK(body["database"] == "connected");
        CHECK(body.contains("uptime_seconds"));
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

TEST_CASE("Cloud API HTTP Handler - Oda ve Mesaj REST Endpoint'leri", "[http][rooms]")
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
        // On hazirlik: Oda olustur
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

    SECTION("GET /api/rooms/{id}/messages mesajlari basariyla listelemeli")
    {
        int64_t room_id = state->db_service.create_room("Sohbet", "2026-09-19 12:00:00");
        state->db_service.save_message(room_id, "User1", "Selamlar", "2026-09-19 12:00:01");

        http::request<http::string_body> req{http::verb::get, "/api/rooms/" + std::to_string(room_id) + "/messages", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        auto body = json::parse(res.body());
        CHECK(body["room_id"] == room_id);
        REQUIRE(body["messages"].size() == 1);
        CHECK(body["messages"][0]["username"] == "User1");
        CHECK(body["messages"][0]["message"] == "Selamlar");
    }

    SECTION("Bilinmeyen endpoint icin 404 Not Found donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/api/tanimsiz", 11};
        auto res = backend::handle_http_request(std::move(req), state);
        CHECK(res.result() == http::status::not_found);
    }
}

TEST_CASE("Boost.Asio IP ve Endpoint Dogrulama Mekanizmasi", "[asio]")
{
    SECTION("Gecerli IPv4 adresi basariyla cozumlenmeli")
    {
        boost::system::error_code ec;
        auto addr = asio::ip::make_address("127.0.0.1", ec);

        REQUIRE_FALSE(ec);
        CHECK(addr.is_v4());
        CHECK(addr.is_loopback());
    }

    SECTION("Hatali IP formati guvenli hata kodu uretmeli")
    {
        boost::system::error_code ec;
        [[maybe_unused]] auto addr = asio::ip::make_address("999.999.999.999", ec);
        CHECK(ec);
    }

    SECTION("TCP Endpoint dogru soket yapilandirmasi uretmeli")
    {
        boost::system::error_code ec;
        auto addr = asio::ip::make_address("192.168.1.1", ec);
        REQUIRE_FALSE(ec);

        asio::ip::tcp::endpoint ep(addr, 8080);
        CHECK(ep.port() == 8080);
        CHECK(ep.address().to_string() == "192.168.1.1");
    }
}