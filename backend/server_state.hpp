#pragma once
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
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

        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        mutable std::mutex sessions_mtx;
        std::vector<std::shared_ptr<WsSession>> sessions;

        void add_session(std::shared_ptr<WsSession> session);
        void remove_session(std::shared_ptr<WsSession> session);
        void broadcast(const std::string &message);
        void broadcast_to_room(int64_t room_id, const std::string &message);
        void evict_from_room(int64_t room_id);
        size_t active_session_count() const;
        size_t room_session_count(int64_t room_id) const;
    };

    class WsSession : public std::enable_shared_from_this<WsSession>
    {
    public:
        explicit WsSession(boost::beast::websocket::stream<boost::beast::tcp_stream> ws)
            : ws_(std::move(ws)), room_id_(-1)
        {
            ws_.text(true);
        }

        bool send(const std::string &msg)
        {
            std::lock_guard<std::mutex> lock(write_mtx_);
            boost::system::error_code ec;
            ws_.write(boost::asio::buffer(msg), ec);
            return !ec;
        }

        boost::beast::websocket::stream<boost::beast::tcp_stream> &stream()
        {
            return ws_;
        }

        void set_room_id(int64_t id) { room_id_.store(id, std::memory_order_release); }
        int64_t get_room_id() const { return room_id_.load(std::memory_order_acquire); }

        void set_username(const std::string &name)
        {
            std::lock_guard<std::mutex> lock(user_mtx_);
            username_ = name;
        }

        std::string get_username() const
        {
            std::lock_guard<std::mutex> lock(user_mtx_);
            return username_;
        }

    private:
        boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
        std::mutex write_mtx_;
        mutable std::mutex user_mtx_;
        std::string username_{"Anonim"};
        std::atomic<int64_t> room_id_;
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
            s->send(message);
    }

    inline void ServerState::broadcast_to_room(int64_t room_id, const std::string &message)
    {
        std::vector<std::shared_ptr<WsSession>> targets;
        {
            std::lock_guard<std::mutex> lock(sessions_mtx);
            for (const auto &s : sessions)
            {
                if (s->get_room_id() == room_id)
                {
                    targets.push_back(s);
                }
            }
        }
        for (auto &s : targets)
            s->send(message);
    }

    inline void ServerState::evict_from_room(int64_t room_id)
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        for (const auto &s : sessions)
        {
            if (s->get_room_id() == room_id)
            {
                s->set_room_id(-1);
            }
        }
    }

    inline size_t ServerState::active_session_count() const
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        return sessions.size();
    }

    inline size_t ServerState::room_session_count(int64_t room_id) const
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        size_t count = 0;
        for (const auto &s : sessions)
        {
            if (s->get_room_id() == room_id)
                count++;
        }
        return count;
    }
}