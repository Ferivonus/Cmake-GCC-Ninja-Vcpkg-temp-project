#pragma once
#include <string>
#include <memory>
#include <boost/asio/ip/tcp.hpp>
#include "backend/server_state.hpp"

namespace backend
{

    class CloudServer
    {
    public:
        CloudServer(const std::string &address, unsigned short port);
        void run();

    private:
        void handle_session(boost::asio::ip::tcp::socket socket);

        std::string address_str_;
        unsigned short port_;
        std::shared_ptr<ServerState> state_;
    };

}