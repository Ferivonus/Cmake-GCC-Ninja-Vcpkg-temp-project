#include "backend/backend.hpp"
#include "backend/http_handler.hpp"
#include "backend/ws_handler.hpp"
#include "config/app_config.hpp"
#include <thread>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

namespace backend
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    using tcp = asio::ip::tcp;

    CloudServer::CloudServer(const std::string &address, unsigned short port)
        : address_str_(address), port_(port), state_(std::make_shared<ServerState>())
    {
        state_->db_service.init("app_chat.db");
    }

    void CloudServer::handle_session(tcp::socket socket)
    {
        try
        {
            beast::tcp_stream stream(std::move(socket));
            beast::flat_buffer buffer;

            http::request<http::string_body> req;
            http::read(stream, buffer, req);

            if (websocket::is_upgrade(req))
            {
                handle_websocket_session(std::move(stream), std::move(req), state_);
                return;
            }

            auto res = handle_http_request(std::move(req), state_);
            http::write(stream, res);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_send, ec);
        }
        catch (const beast::system_error &se)
        {
            if (se.code() != websocket::error::closed)
            {
                AppLog::error("Soket istisnasi: " + std::string(se.what()));
            }
        }
        catch (const std::exception &e)
        {
            AppLog::error("Oturum hatasi: " + std::string(e.what()));
        }
    }

    void CloudServer::run()
    {
        try
        {
            auto const address = asio::ip::make_address(address_str_);
            asio::io_context ioc{1};

            tcp::endpoint endpoint{address, port_};
            tcp::acceptor acceptor{ioc};
            acceptor.open(endpoint.protocol());
            acceptor.set_option(asio::socket_base::reuse_address(true));
            acceptor.bind(endpoint);
            acceptor.listen();

            AppLog::info("Sunucu dinlemede -> http://" + address_str_ + ":" + std::to_string(port_));
            AppLog::info("Health Check   : GET /health");
            AppLog::info("Oda Yonetimi   : GET, POST /api/rooms");
            AppLog::info("Oda CRUD       : GET, PUT, DELETE /api/rooms/{id}");
            AppLog::info("Mesaj Gecmisi  : GET /api/rooms/{id}/messages");
            AppLog::info("Konfigurasyon  : GET, POST, DELETE /api/config");
            AppLog::info("WebSocket      : ws://" + address_str_ + ":" + std::to_string(port_) + "/ws");

            while (true)
            {
                tcp::socket socket{ioc};
                acceptor.accept(socket);

                std::thread([this, s = std::move(socket)]() mutable
                            { this->handle_session(std::move(s)); })
                    .detach();
            }
        }
        catch (const std::exception &e)
        {
            AppLog::error("Sunucu calisma hatasi: " + std::string(e.what()));
        }
    }

} // namespace backend