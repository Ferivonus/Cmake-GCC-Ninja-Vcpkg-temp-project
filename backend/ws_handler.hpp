#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/strand.hpp>
#include <memory>
#include <deque>
#include <string>
#include <atomic>
#include <mutex>

namespace backend
{
    struct ServerState;

    class WsSession : public std::enable_shared_from_this<WsSession>
    {
    public:
        WsSession(boost::asio::ip::tcp::socket &&socket, std::shared_ptr<ServerState> state);

        void run(boost::beast::http::request<boost::beast::http::string_body> req);
        void send(const std::string &message);

        void set_room_id(int64_t id) { room_id_.store(id, std::memory_order_release); }
        int64_t get_room_id() const { return room_id_.load(std::memory_order_acquire); }

        void set_username(const std::string &name);
        std::string get_username() const;

    private:
        void do_read();
        void on_read(boost::beast::error_code ec, std::size_t bytes_transferred);
        void do_write();
        void handle_payload(const std::string &incoming);
        void cleanup();

        boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
        boost::asio::strand<boost::asio::any_io_executor> strand_;
        std::shared_ptr<ServerState> state_;
        boost::beast::flat_buffer buffer_;
        std::deque<std::string> write_queue_;

        std::atomic<int64_t> room_id_{-1};
        mutable std::mutex user_mtx_;
        std::string username_{"Anonim"};
        std::atomic<bool> is_cleaned_{false};
        std::string remote_str_{"bilinmeyen"};
    };
}