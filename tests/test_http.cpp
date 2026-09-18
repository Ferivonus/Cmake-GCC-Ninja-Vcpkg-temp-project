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

TEST_CASE("Cloud API HTTP Handler Yonlendirme ve CRUD Dogrulamasi", "[http][api]")
{
    auto state = std::make_shared<backend::ServerState>();
    state->config = AppConfig{"127.0.0.1", 8080, true};

    SECTION("GET /api/config mevcut yapilandirmayi 200 OK ile donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/api/config", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);
        CHECK(res[http::field::content_type] == "application/json");

        auto body_json = json::parse(res.body());
        CHECK(body_json["host"] == "127.0.0.1");
        CHECK(body_json["port"] == 8080);
        CHECK(body_json["active"] == true);
    }

    SECTION("POST /api/config gecerli JSON ile state guncellemeli ve 200 OK donmeli")
    {
        http::request<http::string_body> req{http::verb::post, "/api/config", 11};
        req.body() = R"({"host":"192.168.1.50","port":9090,"active":false})";
        req.prepare_payload();

        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);

        auto body_json = json::parse(res.body());
        CHECK(body_json["status"] == "success");

        std::lock_guard<std::mutex> lock(state->mtx);
        CHECK(state->config.host == "192.168.1.50");
        CHECK(state->config.port == 9090);
        CHECK(state->config.active == false);
    }

    SECTION("POST /api/config gecersiz JSON verildiginde 400 Bad Request donmeli")
    {
        http::request<http::string_body> req{http::verb::post, "/api/config", 11};
        req.body() = "{ bozuk_json: true ";
        req.prepare_payload();

        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::bad_request);

        auto body_json = json::parse(res.body());
        CHECK(body_json["status"] == "error");
    }

    SECTION("DELETE /api/config yapilandirmayi pasife cekmeli ve 200 OK donmeli")
    {
        http::request<http::string_body> req{http::verb::delete_, "/api/config", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::ok);

        std::lock_guard<std::mutex> lock(state->mtx);
        CHECK(state->config.active == false);
    }

    SECTION("Desteklenmeyen HTTP metodunda 405 Method Not Allowed donmeli")
    {
        http::request<http::string_body> req{http::verb::put, "/api/config", 11};
        auto res = backend::handle_http_request(std::move(req), state);

        CHECK(res.result() == http::status::method_not_allowed);
    }

    SECTION("Bilinmeyen endpoint durumunda 404 Not Found donmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/api/tanimsiz_servis", 11};
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

    SECTION("Gecerli IPv6 adresi basariyla cozumlenmeli")
    {
        boost::system::error_code ec;
        auto addr = asio::ip::make_address("::1", ec);

        REQUIRE_FALSE(ec);
        CHECK(addr.is_v6());
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