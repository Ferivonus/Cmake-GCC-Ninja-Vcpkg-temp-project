#include <catch2/catch_test_macros.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include <sstream>

namespace http = boost::beast::http;
namespace asio = boost::asio;

TEST_CASE("Boost.Beast HTTP/1.1 Istek ve Yanit Islemleri", "[http][beast]")
{
    SECTION("HTTP/1.1 GET istegi standartlara uygun serilestirilmeli")
    {
        http::request<http::string_body> req{http::verb::get, "/api/v1/config", 11};
        req.set(http::field::host, "127.0.0.1:8080");
        req.set(http::field::user_agent, "ModernCpp-App/1.0");

        std::stringstream ss;
        ss << req;
        std::string raw_req = ss.str();

        CHECK(raw_req.find("GET /api/v1/config HTTP/1.1\r\n") != std::string::npos);
        CHECK(raw_req.find("Host: 127.0.0.1:8080\r\n") != std::string::npos);
        CHECK(raw_req.find("User-Agent: ModernCpp-App/1.0\r\n") != std::string::npos);
    }

    SECTION("HTTP/1.1 POST istegi govde ile birlikte serialize edilmeli")
    {
        http::request<http::string_body> post_req{http::verb::post, "/api/v1/save", 11};
        post_req.set(http::field::host, "127.0.0.1:8080");
        post_req.set(http::field::content_type, "application/json");
        post_req.body() = "{\"active\":true}";
        post_req.prepare_payload();

        std::stringstream ss;
        ss << post_req;
        std::string raw_post = ss.str();

        CHECK(raw_post.find("POST /api/v1/save HTTP/1.1\r\n") != std::string::npos);
        CHECK(raw_post.find("Content-Length: 15\r\n") != std::string::npos);
        CHECK(raw_post.find("{\"active\":true}") != std::string::npos);
    }

    SECTION("Ham HTTP 200 OK yaniti basariyla parse edilmeli")
    {
        const std::string body_content = "{\"status\":\"online\"}";
        std::string raw_response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: " +
            std::to_string(body_content.size()) + "\r\n"
                                                  "\r\n" +
            body_content;

        http::response_parser<http::string_body> parser;
        parser.eager(true);
        boost::beast::error_code ec;

        parser.put(asio::buffer(raw_response), ec);

        REQUIRE_FALSE(ec);
        REQUIRE(parser.is_done());

        auto res = parser.get();
        CHECK(res.result() == http::status::ok);
        CHECK(res[http::field::content_type] == "application/json");
        CHECK(res.body() == "{\"status\":\"online\"}");
    }

    SECTION("Bozuk HTTP yanitinda guvenli hata kodu uretilmeli")
    {
        std::string corrupted_response = "NOT_A_VALID_HTTP_PACKET\r\n\r\n";

        http::response_parser<http::string_body> parser;
        boost::beast::error_code ec;

        parser.put(asio::buffer(corrupted_response), ec);

        CHECK(ec);
        CHECK_FALSE(parser.is_done());
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