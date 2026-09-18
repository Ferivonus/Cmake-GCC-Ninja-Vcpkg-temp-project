#pragma once
#include <mutex>
#include <memory>
#include <vector>
#include <algorithm>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include "config/app_config.hpp"
#include "services/json_service.hpp"
#include "services/db_service.hpp"

namespace backend
{
    class WsSession;

    struct ServerState
    {
        AppConfig config{"127.0.0.1", 8080, true};
        std::mutex mtx;
        JsonService json_service;
        DbService db_service;

        std::mutex sessions_mtx;
        std::vector<std::shared_ptr<WsSession>> sessions;

        void add_session(std::shared_ptr<WsSession> session);
        void remove_session(std::shared_ptr<WsSession> session);
        void broadcast(const std::string &message);
    };

    class WsSession : public std::enable_shared_from_this<WsSession>
    {
    public:
        explicit WsSession(boost::beast::websocket::stream<boost::beast::tcp_stream> ws)
            : ws_(std::move(ws)) {}

        void send(const std::string &msg)
        {
            std::lock_guard<std::mutex> lock(write_mtx_);
            boost::system::error_code ec;
            ws_.text(true);
            ws_.write(boost::asio::buffer(msg), ec);
        }

        boost::beast::websocket::stream<boost::beast::tcp_stream> &stream()
        {
            return ws_;
        }

    private:
        boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
        std::mutex write_mtx_;
    };

    inline void ServerState::add_session(std::shared_ptr<WsSession> session)
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        sessions.push_back(session);
    }

    inline void ServerState::remove_session(std::shared_ptr<WsSession> session)
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        sessions.erase(std::remove(sessions.begin(), sessions.end(), session), sessions.end());
    }

    inline void ServerState::broadcast(const std::string &message)
    {
        std::vector<std::shared_ptr<WsSession>> targets;
        {
            std::lock_guard<std::mutex> lock(sessions_mtx);
            targets = sessions;
        }
        for (auto &s : targets)
        {
            s->send(message);
        }
    }

}