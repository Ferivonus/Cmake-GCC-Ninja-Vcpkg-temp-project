#include "backend/ws_handler.hpp"
#include "config/app_config.hpp"
#include <boost/asio/buffer.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>

namespace backend
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
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

    void handle_websocket_session(
        beast::tcp_stream stream,
        boost::beast::http::request<boost::beast::http::string_body> req,
        std::shared_ptr<ServerState> state)
    {
        std::shared_ptr<WsSession> session;
        try
        {
            websocket::stream<beast::tcp_stream> ws(std::move(stream));
            ws.accept(req);
            AppLog::info("WebSocket baglantisi acildi: " + std::string(req.target()));

            session = std::make_shared<WsSession>(std::move(ws));
            state->add_session(session);

            for (;;)
            {
                beast::flat_buffer ws_buf;
                session->stream().read(ws_buf);

                std::string incoming = beast::buffers_to_string(ws_buf.data());

                try
                {
                    auto data = json::parse(incoming);
                    if (!data.is_object())
                    {
                        session->send(R"({"type":"error","message":"JSON payload bir nesne (object) olmalidir"})");
                        continue;
                    }

                    std::string action = data.value("action", "message");

                    // 1. Odaya katılma
                    if (action == "join")
                    {
                        if (!data.contains("room_id") || !data["room_id"].is_number_integer())
                        {
                            session->send(R"({"type":"error","message":"Gecersiz 'room_id'"})");
                            continue;
                        }

                        int64_t target_room = data["room_id"].get<int64_t>();
                        auto room = state->db_service.get_room(target_room);

                        if (!room)
                        {
                            session->send(R"({"type":"error","message":"Oda bulunamadi"})");
                            continue;
                        }

                        if (!room->is_open)
                        {
                            session->send(R"({"type":"error","message":"Bu oda kapatilmistir, giris yapilamaz"})");
                            continue;
                        }

                        int64_t prev_room = session->get_room_id();
                        std::string user = data.value("username", "Anonim");
                        session->set_username(user);

                        // Önceki odadan ayrılma bildirimi
                        if (prev_room > 0 && prev_room != target_room)
                        {
                            json leave_prev = {
                                {"type", "system"},
                                {"event", "user_left"},
                                {"room_id", prev_room},
                                {"username", user},
                                {"timestamp", get_current_timestamp()}};
                            state->broadcast_to_room(prev_room, leave_prev.dump());
                        }

                        session->set_room_id(target_room);

                        json ack = {
                            {"type", "joined_room"},
                            {"room_id", target_room},
                            {"room_name", room->name},
                            {"is_open", room->is_open}};
                        session->send(ack.dump());

                        json broadcast_join = {
                            {"type", "system"},
                            {"event", "user_joined"},
                            {"room_id", target_room},
                            {"username", user},
                            {"timestamp", get_current_timestamp()}};
                        state->broadcast_to_room(target_room, broadcast_join.dump());
                    }
                    // 2. Odadan ayrılma
                    else if (action == "leave")
                    {
                        int64_t prev_room = session->get_room_id();
                        if (prev_room > 0)
                        {
                            session->set_room_id(-1);
                            session->send(json({{"type", "left_room"}, {"room_id", prev_room}}).dump());

                            json broadcast_leave = {
                                {"type", "system"},
                                {"event", "user_left"},
                                {"room_id", prev_room},
                                {"username", session->get_username()},
                                {"timestamp", get_current_timestamp()}};
                            state->broadcast_to_room(prev_room, broadcast_leave.dump());
                        }
                    }
                    // 3. Mesaj iletimi
                    else if (action == "message")
                    {
                        int64_t target_room = (data.contains("room_id") && data["room_id"].is_number_integer())
                                                  ? data["room_id"].get<int64_t>()
                                                  : session->get_room_id();

                        if (target_room <= 0)
                        {
                            session->send(R"({"type":"error","message":"Herhangi bir odaya bagli degilsiniz"})");
                            continue;
                        }

                        if (!state->db_service.is_room_open(target_room))
                        {
                            session->send(R"({"type":"error","message":"Bu oda kapatilmistir, mesaj gonderilemez"})");
                            continue;
                        }

                        if (data.contains("username") && data.contains("message") &&
                            data["username"].is_string() && data["message"].is_string())
                        {
                            std::string user = data["username"].get<std::string>();
                            std::string msg = data["message"].get<std::string>();
                            std::string timestamp = get_current_timestamp();

                            int64_t msg_id = state->db_service.save_message(target_room, user, msg, timestamp);

                            json broadcast_json = {
                                {"type", "chat_message"},
                                {"id", msg_id},
                                {"room_id", target_room},
                                {"username", user},
                                {"message", msg},
                                {"timestamp", timestamp}};

                            state->broadcast_to_room(target_room, broadcast_json.dump());
                        }
                        else
                        {
                            session->send(R"({"type":"error","message":"'username' ve 'message' string olmalidir"})");
                        }
                    }
                    else
                    {
                        session->send(R"({"type":"error","message":"Bilinmeyen 'action' degeri"})");
                    }
                }
                catch (const json::exception &je)
                {
                    session->send(json({{"type", "error"}, {"message", std::string("JSON Hatasi: ") + je.what()}}).dump());
                }
            }
        }
        catch (const beast::system_error &se)
        {
            if (se.code() != websocket::error::closed)
            {
                AppLog::error("WebSocket soket istisnasi: " + std::string(se.what()));
            }
        }
        catch (const std::exception &e)
        {
            AppLog::error("WebSocket oturum hatasi: " + std::string(e.what()));
        }

        if (session)
        {
            int64_t r_id = session->get_room_id();
            if (r_id > 0)
            {
                json disc_evt = {
                    {"type", "system"},
                    {"event", "user_disconnected"},
                    {"room_id", r_id},
                    {"username", session->get_username()},
                    {"timestamp", get_current_timestamp()}};
                state->broadcast_to_room(r_id, disc_evt.dump());
            }

            state->remove_session(session);
            AppLog::info("WebSocket oturumu kapandi ve havuzdan cikarildi.");
        }
    }
}