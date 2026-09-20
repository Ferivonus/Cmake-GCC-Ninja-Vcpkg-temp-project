#include "backend/backend.hpp"
#include "backend/http_handler.hpp"
#include "backend/ws_handler.hpp"
#include "config/app_config.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/strand.hpp>

namespace backend
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    using tcp = asio::ip::tcp;

    // HTTP isteklerini asenkron yöneten oturum sınıfı
    class HttpSession : public std::enable_shared_from_this<HttpSession>
    {
    public:
        HttpSession(tcp::socket &&socket, std::shared_ptr<ServerState> state)
            : stream_(std::move(socket)), state_(std::move(state))
        {
        }

        void run()
        {
            do_read();
        }

    private:
        void do_read()
        {
            req_ = {};
            stream_.expires_after(std::chrono::seconds(30));

            http::async_read(
                stream_, buffer_, req_,
                [self = shared_from_this()](beast::error_code ec, std::size_t)
                {
                    if (ec == http::error::end_of_stream)
                    {
                        self->stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
                        return;
                    }
                    if (ec)
                        return;

                    // Eğer WebSocket Upgrade isteğiyse WsSession'a devret
                    if (websocket::is_upgrade(self->req_))
                    {
                        auto ws_session = std::make_shared<WsSession>(self->stream_.release_socket(), self->state_);
                        ws_session->run(std::move(self->req_));
                        return;
                    }

                    // Standart HTTP isteği
                    auto res = handle_http_request(std::move(self->req_), self->state_);
                    auto is_keep_alive = res.keep_alive();

                    http::async_write(
                        self->stream_, res,
                        [self, is_keep_alive](beast::error_code ec, std::size_t)
                        {
                            if (ec || !is_keep_alive)
                            {
                                self->stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
                                return;
                            }
                            self->do_read();
                        });
                });
        }

        beast::tcp_stream stream_;
        std::shared_ptr<ServerState> state_;
        beast::flat_buffer buffer_;
        http::request<http::string_body> req_;
    };

    // --- CloudServer Implementasyonu ---

    CloudServer::CloudServer(const std::string &address, unsigned short port)
        : address_str_(address),
          port_(port),
          state_(std::make_shared<ServerState>()),
          ioc_(static_cast<int>(std::max(1u, std::thread::hardware_concurrency()))),
          acceptor_(ioc_)
    {
        if (state_->db_service.init("app_chat.db"))
        {
            state_->db_service.ensure_default_room("Konuşma Odası");
        }
    }

    CloudServer::~CloudServer()
    {
        ioc_.stop();
        for (auto &t : thread_pool_)
        {
            if (t.joinable())
                t.join();
        }
    }

    void CloudServer::do_accept()
    {
        acceptor_.async_accept(
            asio::make_strand(ioc_),
            [this](beast::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    std::make_shared<HttpSession>(std::move(socket), state_)->run();
                }
                else
                {
                    AppLog::error("Bağlantı kabul hatası: " + ec.message());
                }

                // Bir sonraki bağlantıyı kabul etmek için dinlemeye devam et
                do_accept();
            });
    }

    void CloudServer::run()
    {
        try
        {
            auto const address = asio::ip::make_address(address_str_);
            tcp::endpoint endpoint{address, port_};

            acceptor_.open(endpoint.protocol());
            acceptor_.set_option(asio::socket_base::reuse_address(true));
            acceptor_.bind(endpoint);
            acceptor_.listen();

            AppLog::info("Asenkron Sunucu Dinlemede -> http://" + address_str_ + ":" + std::to_string(port_));
            AppLog::info("WebSocket Endpoint     -> ws://" + address_str_ + ":" + std::to_string(port_) + "/ws");

            do_accept();

            // İş parçacığı havuzunu başlat
            unsigned int thread_count = std::max(1u, std::thread::hardware_concurrency());
            AppLog::info("İş parçacığı havuzu başlatıldı (" + std::to_string(thread_count) + " thread).");

            thread_pool_.reserve(thread_count);
            for (unsigned int i = 0; i < thread_count; ++i)
            {
                thread_pool_.emplace_back([this]()
                                          { ioc_.run(); });
            }

            for (auto &t : thread_pool_)
            {
                t.join();
            }
        }
        catch (const std::exception &e)
        {
            AppLog::error("Sunucu çalışma hatası: " + std::string(e.what()));
        }
    }
}