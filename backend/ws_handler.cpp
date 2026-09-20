#include "backend/ws_handler.hpp"
#include "backend/server_state.hpp"
#include "config/app_config.hpp"
#include <boost/asio/post.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>

namespace backend
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace asio = boost::asio;
    using json = nlohmann::json;

    static std::string get_current_timestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &in_time_t);
#else
        localtime_r(&in_time_t, &tm_buf);
#endif
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
        return std::string(buffer);
    }

    // --- ServerState Metot Implementasyonları ---
    void ServerState::add_session(std::shared_ptr<WsSession> session)
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        sessions.push_back(session);
    }

    void ServerState::remove_session(std::shared_ptr<WsSession> session)
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        sessions.erase(std::remove(sessions.begin(), sessions.end(), session), sessions.end());
    }

    void ServerState::broadcast_to_room(int64_t room_id, const std::string &message)
    {
        std::vector<std::shared_ptr<WsSession>> targets;
        {
            std::lock_guard<std::mutex> lock(sessions_mtx);
            for (const auto &s : sessions)
            {
                if (s->get_room_id() == room_id)
                    targets.push_back(s);
            }
        }
        for (auto &s : targets)
            s->send(message);
    }

    void ServerState::evict_from_room(int64_t room_id)
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        for (const auto &s : sessions)
        {
            if (s->get_room_id() == room_id)
                s->set_room_id(-1);
        }
    }

    size_t ServerState::active_session_count() const
    {
        std::lock_guard<std::mutex> lock(sessions_mtx);
        return sessions.size();
    }

    size_t ServerState::room_session_count(int64_t room_id) const
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

    // --- WsSession Implementasyonu ---

    WsSession::WsSession(asio::ip::tcp::socket &&socket, std::shared_ptr<ServerState> state)
        : ws_(std::move(socket)),
          state_(std::move(state))
    {
        boost::system::error_code ec;
        auto ep = ws_.next_layer().socket().remote_endpoint(ec);
        if (!ec)
        {
            remote_str_ = ep.address().to_string() + ":" + std::to_string(ep.port());
        }
    }

    void WsSession::set_username(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(user_mtx_);
        username_ = name;
    }

    std::string WsSession::get_username() const
    {
        std::lock_guard<std::mutex> lock(user_mtx_);
        return username_;
    }

    void WsSession::run(beast::http::request<beast::http::string_body> req)
    {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

        // WebSocket el sıkışması (handshake)
        ws_.async_accept(
            req,
            [self = shared_from_this()](beast::error_code ec)
            {
                if (ec)
                {
                    AppLog::error("WS Handshake hatasi: " + ec.message());
                    return;
                }

                AppLog::info("WebSocket baglantisi kuruldu: " + self->remote_str_);
                self->state_->add_session(self);
                self->do_read();
            });
    }

    void WsSession::do_read()
    {
        ws_.async_read(
            buffer_,
            [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred)
            {
                self->on_read(ec, bytes_transferred);
            });
    }

    void WsSession::on_read(beast::error_code ec, std::size_t)
    {
        if (ec == websocket::error::closed || ec == asio::error::operation_aborted || ec == asio::error::eof)
        {
            cleanup();
            return;
        }

        if (ec)
        {
            AppLog::error("WS Okuma hatasi: " + ec.message());
            cleanup();
            return;
        }

        std::string incoming = beast::buffers_to_string(buffer_.data());
        buffer_.consume(buffer_.size());

        handle_payload(incoming);
        do_read(); // Bir sonraki mesajı dinlemeye devam et
    }

    void WsSession::handle_payload(const std::string &incoming)
    {
        try
        {
            auto data = json::parse(incoming);
            if (!data.is_object())
            {
                send(R"({"type":"error","message":"JSON payload bir nesne (object) olmalidir"})");
                return;
            }

            std::string action = data.value("action", "message");

            // 1. Odaya Katılma
            if (action == "join")
            {
                if (!data.contains("room_id") || !data["room_id"].is_number_integer())
                {
                    send(R"({"type":"error","message":"Gecersiz 'room_id'"})");
                    return;
                }

                int64_t target_room = data["room_id"].get<int64_t>();
                auto room = state_->db_service.get_room(target_room);

                if (!room)
                {
                    send(R"({"type":"error","message":"Oda bulunamadi"})");
                    return;
                }

                if (!room->is_open)
                {
                    send(R"({"type":"error","message":"Bu oda kapatilmistir, giris yapilamaz"})");
                    return;
                }

                int64_t prev_room = get_room_id();
                std::string user = data.value("username", "Anonim");
                set_username(user);

                if (prev_room > 0 && prev_room != target_room)
                {
                    json leave_prev = {
                        {"type", "system"},
                        {"event", "user_left"},
                        {"room_id", prev_room},
                        {"username", user},
                        {"timestamp", get_current_timestamp()}};
                    state_->broadcast_to_room(prev_room, leave_prev.dump());
                }

                set_room_id(target_room);

                json ack = {
                    {"type", "joined_room"},
                    {"room_id", target_room},
                    {"room_name", room->name},
                    {"is_open", room->is_open}};
                send(ack.dump());

                json broadcast_join = {
                    {"type", "system"},
                    {"event", "user_joined"},
                    {"room_id", target_room},
                    {"username", user},
                    {"timestamp", get_current_timestamp()}};
                state_->broadcast_to_room(target_room, broadcast_join.dump());
            }
            // 2. Odadan Ayrılma
            else if (action == "leave")
            {
                int64_t prev_room = get_room_id();
                if (prev_room > 0)
                {
                    set_room_id(-1);
                    send(json({{"type", "left_room"}, {"room_id", prev_room}}).dump());

                    json broadcast_leave = {
                        {"type", "system"},
                        {"event", "user_left"},
                        {"room_id", prev_room},
                        {"username", get_username()},
                        {"timestamp", get_current_timestamp()}};
                    state_->broadcast_to_room(prev_room, broadcast_leave.dump());
                }
            }
            // 3. Mesaj İletimi & At-Rest Şifreleme
            else if (action == "message")
            {
                int64_t target_room = get_room_id();
                if (target_room <= 0)
                {
                    send(R"({"type":"error","message":"Herhangi bir odaya bagli degilsiniz"})");
                    return;
                }

                if (!state_->db_service.is_room_open(target_room))
                {
                    send(R"({"type":"error","message":"Bu oda kapatilmistir, mesaj gonderilemez"})");
                    return;
                }

                if (data.contains("message") && data["message"].is_string())
                {
                    std::string user = get_username();
                    std::string msg = data["message"].get<std::string>();
                    std::string timestamp = get_current_timestamp();

                    // Veritabanı için AES-256-GCM ile saklama
                    std::string storage_text = msg;
                    auto encrypted = crypto::CryptoService::encrypt(msg, state_->db_cipher_key);
                    if (encrypted)
                    {
                        json enc_envelope = {
                            {"__enc", true},
                            {"c", encrypted->ciphertext_base64},
                            {"iv", encrypted->iv_base64},
                            {"tag", encrypted->tag_base64}};
                        storage_text = enc_envelope.dump();
                    }

                    int64_t msg_id = state_->db_service.save_message(target_room, user, storage_text, timestamp);

                    json broadcast_json = {
                        {"type", "chat_message"},
                        {"id", msg_id},
                        {"room_id", target_room},
                        {"username", user},
                        {"message", msg},
                        {"timestamp", timestamp}};

                    state_->broadcast_to_room(target_room, broadcast_json.dump());
                }
            }
            else
            {
                send(R"({"type":"error","message":"Bilinmeyen 'action' komutu"})");
            }
        }
        catch (const std::exception &e)
        {
            send(json({{"type", "error"}, {"message", std::string("Hata: ") + e.what()}}).dump());
        }
    }

    void WsSession::send(const std::string &message)
    {
        // Kuyruk ekleme ve yazma tetikleme işini soketin yürütücüsüne (strand) post ediyoruz
        asio::post(
            ws_.get_executor(),
            [self = shared_from_this(), message]()
            {
                self->write_queue_.push_back(message);
                if (self->write_queue_.size() == 1)
                {
                    self->do_write();
                }
            });
    }

    void WsSession::do_write()
    {
        ws_.text(true);
        ws_.async_write(
            asio::buffer(write_queue_.front()),
            [self = shared_from_this()](beast::error_code ec, std::size_t)
            {
                if (ec)
                {
                    AppLog::error("WS Yazma hatasi: " + ec.message());
                    self->cleanup();
                    return;
                }

                self->write_queue_.pop_front();
                if (!self->write_queue_.empty())
                {
                    self->do_write();
                }
            });
    }

    void WsSession::cleanup()
    {
        bool expected = false;
        if (!is_cleaned_.compare_exchange_strong(expected, true))
            return;

        int64_t r_id = get_room_id();
        if (r_id > 0)
        {
            json disc_evt = {
                {"type", "system"},
                {"event", "user_disconnected"},
                {"room_id", r_id},
                {"username", get_username()},
                {"timestamp", get_current_timestamp()}};
            state_->broadcast_to_room(r_id, disc_evt.dump());
        }

        state_->remove_session(shared_from_this());
        AppLog::info("WebSocket oturumu temizlendi (" + remote_str_ + ")");
    }
}