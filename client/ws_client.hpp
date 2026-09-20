#pragma once
#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <boost/asio/ip/tcp.hpp>

namespace client
{
    class WsClient
    {
    public:
        WsClient(std::string host, unsigned short port, std::string username,
                 int64_t initial_room_id = -1,
                 bool use_tor = false,
                 std::string proxy_host = "127.0.0.1",
                 unsigned short proxy_port = 9050);

        void run_interactive();

    private:
        bool perform_socks5_handshake(boost::asio::ip::tcp::socket &socket);
        void print_line(const std::string &line, bool print_prompt = true);

        std::string host_;
        unsigned short port_;
        std::string username_;
        std::atomic<int64_t> current_room_id_;
        bool use_tor_;
        std::string proxy_host_;
        unsigned short proxy_port_;
        mutable std::mutex console_mtx_;
    };
}