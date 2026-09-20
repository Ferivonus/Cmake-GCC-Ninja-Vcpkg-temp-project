#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <boost/asio/ip/tcp.hpp>
#include "services/crypto_service.hpp"

namespace client
{
    class WsClient
    {
    public:
        // Kurucu parametre sırası: username, host, port, initial_room_id, initial_password, use_tor, proxy_host, proxy_port
        WsClient(std::string username,
                 std::string host,
                 unsigned short port,
                 int64_t initial_room_id = -1,
                 std::string initial_password = "",
                 bool use_tor = false,
                 std::string proxy_host = "127.0.0.1",
                 unsigned short proxy_port = 9050);

        void run_interactive();

    private:
        bool perform_socks5_handshake(boost::asio::ip::tcp::socket &socket);
        void print_line(const std::string &line, bool print_prompt = true);

        std::string username_;
        std::string host_;
        unsigned short port_;
        std::atomic<int64_t> current_room_id_;
        bool use_tor_;
        std::string proxy_host_;
        unsigned short proxy_port_;

        mutable std::mutex console_mtx_;
        mutable std::mutex write_mtx_; // Sokete eşzamanlı yazmayı koruyan kilit

        // E2EE Kripto Durumu (AES-256 anahtarı ve eşzamanlılık kilidi)
        std::vector<uint8_t> current_room_key_;
        mutable std::mutex key_mtx_;
    };
}