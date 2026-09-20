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
#include <sstream>

namespace client
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = asio::ip::tcp;
    using json = nlohmann::json;

    WsClient::WsClient(std::string host, unsigned short port, std::string username,
                       int64_t initial_room_id, bool use_tor,
                       std::string proxy_host, unsigned short proxy_port)
        : host_(std::move(host)), port_(port), username_(std::move(username)),
          current_room_id_(initial_room_id), use_tor_(use_tor),
          proxy_host_(std::move(proxy_host)), proxy_port_(proxy_port)
    {
        if (host_.find(".onion") != std::string::npos)
        {
            use_tor_ = true;
        }
    }

    void WsClient::print_line(const std::string &line, bool print_prompt)
    {
        std::lock_guard<std::mutex> lock(console_mtx_);
        std::cout << "\r" << line << "\n";
        if (print_prompt)
        {
            std::cout << "> " << std::flush;
        }
        else
        {
            std::cout << std::flush;
        }
    }

    bool WsClient::perform_socks5_handshake(tcp::socket &socket)
    {
        if (host_.size() > 255)
        {
            AppLog::error("Hedef domain/adres boyutu 255 karakteri asamaz.");
            return false;
        }

        boost::system::error_code ec;

        // 1. SOCKS5 Metot Anlaşması (0x05: SOCKS v5, 0x01: 1 metot, 0x00: Kimlik Doğrulamasız)
        const uint8_t greeting[] = {0x05, 0x01, 0x00};
        asio::write(socket, asio::buffer(greeting), ec);
        if (ec)
            return false;

        uint8_t greeting_response[2];
        asio::read(socket, asio::buffer(greeting_response), ec);
        if (ec || greeting_response[0] != 0x05 || greeting_response[1] != 0x00)
        {
            AppLog::error("Tor SOCKS5 kimlik dogrulama basarisiz veya vekil tarafindan reddedildi.");
            return false;
        }

        // 2. CONNECT Talebi: DNS sızıntısını önlemek için ATYP=0x03 (Domain Adı)
        std::vector<uint8_t> req;
        req.reserve(7 + host_.size());
        req.push_back(0x05); // SOCKS sürümü
        req.push_back(0x01); // Komut: CONNECT
        req.push_back(0x00); // Rezerve bayt
        req.push_back(0x03); // Adres tipi: Alan adı (Domain Name)
        req.push_back(static_cast<uint8_t>(host_.size()));
        req.insert(req.end(), host_.begin(), host_.end());
        req.push_back(static_cast<uint8_t>((port_ >> 8) & 0xFF));
        req.push_back(static_cast<uint8_t>(port_ & 0xFF));

        asio::write(socket, asio::buffer(req), ec);
        if (ec)
            return false;

        uint8_t resp_header[4];
        asio::read(socket, asio::buffer(resp_header), ec);
        if (ec || resp_header[0] != 0x05 || resp_header[1] != 0x00)
        {
            AppLog::error("Tor vekili hedef sunucuya baglanamadi (Durum Kodu: " + std::to_string(resp_header[1]) + ")");
            return false;
        }

        // Kalan vekil yanıt baytlarını (BND.ADDR ve BND.PORT) güvenle tüket
        if (resp_header[3] == 0x01) // IPv4
        {
            uint8_t dummy[6];
            asio::read(socket, asio::buffer(dummy), ec);
        }
        else if (resp_header[3] == 0x03) // Alan adı
        {
            uint8_t len = 0;
            asio::read(socket, asio::buffer(&len, 1), ec);
            std::vector<uint8_t> dummy(len + 2);
            asio::read(socket, asio::buffer(dummy), ec);
        }
        else if (resp_header[3] == 0x04) // IPv6
        {
            uint8_t dummy[18];
            asio::read(socket, asio::buffer(dummy), ec);
        }
        else
        {
            return false;
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

                AppLog::info("Tor tüneli kuruluyor -> " + host_ + ":" + std::to_string(port_));
                if (!perform_socks5_handshake(beast::get_lowest_layer(ws).socket()))
                {
                    AppLog::error("Tor devre kurulumu basarisiz oldu.");
                    return;
                }
                AppLog::info("Tor devresi kuruldu.");
            }
            else
            {
                AppLog::info("Baglanti kuruluyor -> " + host_ + ":" + std::to_string(port_));
                auto const results = resolver.resolve(host_, std::to_string(port_));
                beast::get_lowest_layer(ws).connect(results);
            }

            const std::string host_header = host_ + ":" + std::to_string(port_);
            ws.handshake(host_header, "/ws");
            AppLog::info("WebSocket baglantisi aktif. Oturum: " + username_);

            // Başlangıç odasına katılım (Eğer kullanıcı açıkça -r verdiyse)
            const int64_t init_room = current_room_id_.load(std::memory_order_relaxed);
            if (init_room > 0)
            {
                json join_req = {
                    {"action", "join"},
                    {"room_id", init_room},
                    {"username", username_}};
                ws.text(true);
                ws.write(asio::buffer(join_req.dump()));
            }

            {
                std::lock_guard<std::mutex> lock(console_mtx_);
                std::cout << "\n=======================================================\n"
                          << " CANLI CHAT OTURUMU (" << (use_tor_ ? "TOR VEKILI" : "DOGRADAN TCP") << ")\n"
                          << " Durum   : " << (init_room > 0 ? ("Oda #" + std::to_string(init_room)) : "LOBI (Bosta)") << "\n"
                          << " Komutlar: /join <id>, /leave, /room, /help, exit\n"
                          << "=======================================================\n";
                if (init_room <= 0)
                {
                    std::cout << "[Bilgi] Sunucuya baglisiniz (Cevrim ici). Bir odaya katilmak icin: /join <oda_id>\n\n";
                }
                else
                {
                    std::cout << "\n";
                }
            }

            std::atomic<bool> is_running{true};

            // Eşzamanlı Dinleyici İş Parçacığı (Reader Thread)
            std::thread reader_thread([&]()
                                      {
                try
                {
                    while (is_running.load(std::memory_order_relaxed))
                    {
                        beast::flat_buffer buffer;
                        boost::system::error_code ec;
                        ws.read(buffer, ec);

                        if (ec)
                        {
                            if (is_running.load(std::memory_order_relaxed))
                            {
                                print_line("[Sistem] Sunucu baglantisi kapandi (" + ec.message() + "). Cikmak icin Enter'a basiniz.", false);
                            }
                            break;
                        }

                        const std::string incoming = beast::buffers_to_string(buffer.data());
                        try
                        {
                            const auto res_json = json::parse(incoming);
                            if (!res_json.is_object())
                                continue;

                            const std::string type = res_json.value("type", "");

                            if (type == "chat_message")
                            {
                                const std::string user = res_json.value("username", "Anonim");
                                const std::string msg = res_json.value("message", "");
                                const std::string time_str = res_json.value("timestamp", "--:--:--");
                                const int64_t r_id = res_json.value("room_id", int64_t{0});

                                if (user != username_)
                                {
                                    print_line("[" + time_str + "] [Oda #" + std::to_string(r_id) + "] " + user + ": " + msg);
                                }
                            }
                            else if (type == "joined_room")
                            {
                                const int64_t r_id = res_json.value("room_id", int64_t{-1});
                                const std::string r_name = res_json.value("room_name", "");
                                current_room_id_.store(r_id, std::memory_order_release);

                                print_line("[Sistem] Odaya gecildi: '" + r_name + "' (ID: #" + std::to_string(r_id) + ")");
                            }
                            else if (type == "left_room")
                            {
                                current_room_id_.store(-1, std::memory_order_release);
                                print_line("[Sistem] Odadan cikildi. Su an lobi modundasiniz (/join <id> ile odaya katilabilirsiniz).");
                            }
                            else if (type == "system")
                            {
                                const std::string event = res_json.value("event", "");
                                const std::string user = res_json.value("username", "");
                                const int64_t r_id = res_json.value("room_id", int64_t{0});

                                if (event == "user_joined" && user != username_)
                                {
                                    print_line("[Sistem] " + user + " odaya (#" + std::to_string(r_id) + ") katildi.");
                                }
                                else if (event == "user_left" && user != username_)
                                {
                                    print_line("[Sistem] " + user + " odadan (#" + std::to_string(r_id) + ") ayrildi.");
                                }
                                else if (event == "user_disconnected")
                                {
                                    print_line("[Sistem] " + user + " baglantisini kaybetti.");
                                }
                                else if (event == "room_closed")
                                {
                                    print_line("[Uyari] Bulundugunuz oda kapatilmistir! Mesaj gonderilemez.");
                                }
                                else if (event == "room_deleted")
                                {
                                    current_room_id_.store(-1, std::memory_order_release);
                                    print_line("[Uyari] Bulundugunuz oda silindi! Lobi moduna dondunuz.");
                                }
                            }
                            else if (type == "error")
                            {
                                print_line("[Hata] " + res_json.value("message", "Bilinmeyen hata"));
                            }
                        }
                        catch (const json::exception &)
                        {
                        }
                    }
                }
                catch (...)
                {
                } });

            // Ana İstemci Giriş Döngüsü
            std::string line;
            while (is_running.load(std::memory_order_relaxed))
            {
                {
                    std::lock_guard<std::mutex> lock(console_mtx_);
                    std::cout << "> " << std::flush;
                }

                if (!std::getline(std::cin, line))
                {
                    is_running.store(false, std::memory_order_release);
                    break;
                }

                if (line.empty())
                    continue;

                if (line == "exit" || line == "quit")
                {
                    is_running.store(false, std::memory_order_release);
                    break;
                }

                // Komut İşleme Bloğu
                if (line[0] == '/')
                {
                    std::istringstream iss(line);
                    std::string cmd;
                    iss >> cmd;

                    if (cmd == "/join")
                    {
                        int64_t target_room = 0;
                        if (iss >> target_room && target_room > 0)
                        {
                            json join_msg = {
                                {"action", "join"},
                                {"room_id", target_room},
                                {"username", username_}};
                            boost::system::error_code ec;
                            ws.text(true);
                            ws.write(asio::buffer(join_msg.dump()), ec);
                            if (ec)
                                break;
                        }
                        else
                        {
                            print_line("[Kullanim] /join <oda_id> (Orn: /join 2)");
                        }
                    }
                    else if (cmd == "/leave")
                    {
                        json leave_msg = {{"action", "leave"}};
                        boost::system::error_code ec;
                        ws.text(true);
                        ws.write(asio::buffer(leave_msg.dump()), ec);
                        if (ec)
                            break;
                    }
                    else if (cmd == "/room")
                    {
                        const int64_t current = current_room_id_.load(std::memory_order_relaxed);
                        if (current > 0)
                            print_line("[Durum] Bagli bulunulan oda: #" + std::to_string(current));
                        else
                            print_line("[Durum] Herhangi bir odaya bagli degilsiniz (Lobi modundasiniz).");
                    }
                    else if (cmd == "/help")
                    {
                        print_line("Kullanilabilir komutlar:\n"
                                   "  /join <id>  : Belirtilen odaya gecis yapar\n"
                                   "  /leave      : Odadan ayrilarak lobi moduna gecer\n"
                                   "  /room       : Mevcut oda bilgisini gosterir\n"
                                   "  /help       : Bu yardim menusunu basar\n"
                                   "  exit, quit  : Programi guvenle sonlandirir");
                    }
                    else
                    {
                        print_line("[Hata] Gecersiz komut. Desteklenen komutlar icin /help yaziniz.");
                    }
                    continue;
                }

                // Normal Mesaj Gönderimi
                const int64_t active_room = current_room_id_.load(std::memory_order_relaxed);
                if (active_room <= 0)
                {
                    print_line("[Uyari] Mesaj iletmek icin bir odaya girmelisiniz: /join <id>");
                    continue;
                }

                json req_payload = {
                    {"action", "message"},
                    {"room_id", active_room},
                    {"username", username_},
                    {"message", line}};

                boost::system::error_code ec;
                ws.text(true);
                ws.write(asio::buffer(req_payload.dump()), ec);
                if (ec)
                {
                    AppLog::error("Ileti transfer hatasi: " + ec.message());
                    break;
                }
            }

            is_running.store(false, std::memory_order_release);

            // Soket Bağlantısını Güvenle Kapatma
            boost::system::error_code ec;
            beast::get_lowest_layer(ws).socket().shutdown(tcp::socket::shutdown_both, ec);
            beast::get_lowest_layer(ws).socket().close(ec);

            if (reader_thread.joinable())
            {
                reader_thread.join();
            }

            AppLog::info("Istemci soket oturumu guvenle kapatildi.");
        }
        catch (const std::exception &e)
        {
            AppLog::error("Istemci calisma zamani hatasi: " + std::string(e.what()));
        }
    }
}