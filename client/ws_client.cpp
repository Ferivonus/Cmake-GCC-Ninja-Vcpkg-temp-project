#include "client/ws_client.hpp"
#include "config/app_config.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

namespace client
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = asio::ip::tcp;
    using json = nlohmann::json;

    WsClient::WsClient(std::string host, unsigned short port, std::string username,
                       bool use_tor, std::string proxy_host, unsigned short proxy_port)
        : host_(std::move(host)), port_(port), username_(std::move(username)),
          use_tor_(use_tor), proxy_host_(std::move(proxy_host)), proxy_port_(proxy_port)
    {
        if (host_.find(".onion") != std::string::npos)
        {
            use_tor_ = true;
        }
    }

    bool WsClient::perform_socks5_handshake(tcp::socket &socket)
    {
        boost::system::error_code ec;

        // 1. SOCKS5 Tanitimi
        const uint8_t greeting[] = {0x05, 0x01, 0x00};
        asio::write(socket, asio::buffer(greeting), ec);
        if (ec)
            return false;

        uint8_t greeting_response[2];
        asio::read(socket, asio::buffer(greeting_response), ec);
        if (ec || greeting_response[0] != 0x05 || greeting_response[1] != 0x00)
        {
            AppLog::error("Tor SOCKS5 kimlik dogrulama basarisiz veya reddedildi.");
            return false;
        }

        // 2. Baglanti Istegi: ATYP=0x03 ile DNS sizintisi onlenir
        std::vector<uint8_t> req;
        req.push_back(0x05);                                      // VER
        req.push_back(0x01);                                      // CMD (CONNECT)
        req.push_back(0x00);                                      // RSV
        req.push_back(0x03);                                      // ATYP (Domain name)
        req.push_back(static_cast<uint8_t>(host_.size()));        // Domain uzunlugu
        req.insert(req.end(), host_.begin(), host_.end());        // Domain degeri
        req.push_back(static_cast<uint8_t>((port_ >> 8) & 0xFF)); // Port (Big-endian)
        req.push_back(static_cast<uint8_t>(port_ & 0xFF));

        asio::write(socket, asio::buffer(req), ec);
        if (ec)
            return false;

        uint8_t resp_header[4];
        asio::read(socket, asio::buffer(resp_header), ec);
        if (ec || resp_header[1] != 0x00)
        {
            AppLog::error("Tor vekili hedef sunucuya baglanamadi (Hata kodu: " + std::to_string(resp_header[1]) + ")");
            return false;
        }

        if (resp_header[3] == 0x01)
        {
            uint8_t dummy[6];
            asio::read(socket, asio::buffer(dummy), ec);
        }
        else if (resp_header[3] == 0x03)
        {
            uint8_t len = 0;
            asio::read(socket, asio::buffer(&len, 1), ec);
            std::vector<uint8_t> dummy(len + 2);
            asio::read(socket, asio::buffer(dummy), ec);
        }
        else if (resp_header[3] == 0x04)
        {
            uint8_t dummy[18];
            asio::read(socket, asio::buffer(dummy), ec);
        }

        return !ec;
    }

    void WsClient::run_interactive()
    {
        try
        {
            asio::io_context ioc;
            tcp::resolver resolver(ioc);
            websocket::stream<beast::tcp_stream> ws(ioc);

            if (use_tor_)
            {
                AppLog::info("Tor SOCKS5 vekiline baglaniliyor -> " + proxy_host_ + ":" + std::to_string(proxy_port_));
                auto const proxy_endpoints = resolver.resolve(proxy_host_, std::to_string(proxy_port_));
                beast::get_lowest_layer(ws).connect(proxy_endpoints);

                AppLog::info("Tor uzerinden hedef rota kuruluyor -> " + host_ + ":" + std::to_string(port_));
                if (!perform_socks5_handshake(beast::get_lowest_layer(ws).socket()))
                {
                    AppLog::error("Tor SOCKS5 baglantisi basarisiz oldu. Tor servisinin calistigindan emin olun.");
                    return;
                }
                AppLog::info("Tor devresi kuruldu.");
            }
            else
            {
                AppLog::info("Dogrudan baglanti kuruluyor -> " + host_ + ":" + std::to_string(port_));
                auto const results = resolver.resolve(host_, std::to_string(port_));
                beast::get_lowest_layer(ws).connect(results);
            }

            std::string host_header = host_ + ":" + std::to_string(port_);
            ws.handshake(host_header, "/ws");
            AppLog::info("WebSocket baglantisi kuruldu. Kullanici: " + username_);

            std::cout << "\n=== CANLI CHAT BASLADI (" << (use_tor_ ? "TOR AKTIF" : "DOGRADAN") << ") (Cikmak icin 'exit' yazin) ===\n";

            std::atomic<bool> is_running{true};

            std::thread reader_thread([&]()
                                      {
                try
                {
                    while (is_running)
                    {
                        beast::flat_buffer buffer;
                        boost::system::error_code ec;
                        ws.read(buffer, ec);

                        if (ec)
                        {
                            if (is_running)
                            {
                                std::cout << "\n[Bilgi] Sunucu baglantisi kapandi.\n";
                            }
                            break;
                        }

                        std::string incoming = beast::buffers_to_string(buffer.data());
                        try
                        {
                            auto res_json = json::parse(incoming);
                            std::string type = res_json.value("type", "");

                            if (type == "chat_message")
                            {
                                std::string user = res_json.value("username", "Anonim");
                                std::string msg = res_json.value("message", "");
                                std::string time_str = res_json.value("timestamp", "--:--:--");

                                if (user != username_)
                                {
                                    std::cout << "\r[" << time_str << "] [" << user << "]: " << msg << "\n"
                                              << username_ << " > " << std::flush;
                                }
                            }
                        }
                        catch (...)
                        {
                        }
                    }
                }
                catch (...)
                {
                } });

            std::string line;
            while (is_running)
            {
                std::cout << username_ << " > " << std::flush;
                if (!std::getline(std::cin, line) || line == "exit" || line == "quit")
                {
                    is_running = false;
                    break;
                }

                if (line.empty())
                {
                    continue;
                }

                json req_payload = {
                    {"username", username_},
                    {"message", line}};

                boost::system::error_code ec;
                ws.text(true);
                ws.write(asio::buffer(req_payload.dump()), ec);
                if (ec)
                {
                    AppLog::error("Mesaj iletilemedi: " + ec.message());
                    break;
                }
            }

            is_running = false;

            boost::system::error_code ec;
            beast::get_lowest_layer(ws).socket().shutdown(tcp::socket::shutdown_both, ec);

            if (reader_thread.joinable())
            {
                reader_thread.join();
            }

            beast::get_lowest_layer(ws).socket().close(ec);

            AppLog::info("Baglanti kapatildi.");
        }
        catch (const std::exception &e)
        {
            AppLog::error("Client hatasi: " + std::string(e.what()));
        }
    }
}