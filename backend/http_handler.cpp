#include "backend/http_handler.hpp"

namespace backend
{
    namespace http = boost::beast::http;

    http::response<http::string_body> handle_http_request(
        http::request<http::string_body> &&req,
        std::shared_ptr<ServerState> state)
    {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "ModernApp-Cloud/1.0");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());

        if (req.target() == "/api/config")
        {
            if (req.method() == http::verb::get)
            {
                std::lock_guard<std::mutex> lock(state->mtx);
                res.body() = state->json_service.serialize(state->config);
            }
            else if (req.method() == http::verb::post)
            {
                auto parsed = state->json_service.deserialize(req.body());
                if (parsed)
                {
                    std::lock_guard<std::mutex> lock(state->mtx);
                    state->config = *parsed;
                    res.body() = R"({"status":"success","message":"Ayar guncellendi"})";
                }
                else
                {
                    res.result(http::status::bad_request);
                    res.body() = R"({"status":"error","message":"Gecersiz JSON"})";
                }
            }
            else if (req.method() == http::verb::delete_)
            {
                std::lock_guard<std::mutex> lock(state->mtx);
                state->config = AppConfig{"127.0.0.1", 8080, false};
                res.body() = R"({"status":"success","message":"Ayar silindi/sifirlandi"})";
            }
            else
            {
                res.result(http::status::method_not_allowed);
                res.body() = R"({"error":"Izin verilmeyen metot"})";
            }
        }
        else
        {
            res.result(http::status::not_found);
            res.body() = R"({"error":"Endpoint bulunamadi"})";
        }

        res.prepare_payload();
        return res;
    }

}