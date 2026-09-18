#pragma once
#include <string>
#include <boost/asio/ip/tcp.hpp>

namespace client
{
    class WsClient
    {
    public:
        WsClient(std::string host, unsigned short port, std::string username,
                 bool use_tor = false,
                 std::string proxy_host = "127.0.0.1",
                 unsigned short proxy_port = 9050);

        void run_interactive();

    private:
        bool perform_socks5_handshake(boost::asio::ip::tcp::socket &socket);

        std::string host_;
        unsigned short port_;
        std::string username_;
        bool use_tor_;
        std::string proxy_host_;
        unsigned short proxy_port_;
    };
}