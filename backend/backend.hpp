#pragma once
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "backend/server_state.hpp"

namespace backend
{
    class CloudServer
    {
    public:
        CloudServer(const std::string &address, unsigned short port);
        ~CloudServer();
        void run();

    private:
        void do_accept();

        std::string address_str_;
        unsigned short port_;
        std::shared_ptr<ServerState> state_;

        boost::asio::io_context ioc_;
        boost::asio::ip::tcp::acceptor acceptor_;
        std::vector<std::thread> thread_pool_;
    };
}