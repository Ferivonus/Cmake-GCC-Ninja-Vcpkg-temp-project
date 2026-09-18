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
            AppLog::info("WebSocket baglantisi kabul edildi: " + std::string(req.target()));

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

                    if (data.contains("username") && data.contains("message") &&
                        data["username"].is_string() && data["message"].is_string())
                    {
                        std::string user = data["username"].get<std::string>();
                        std::string msg = data["message"].get<std::string>();

                        std::string timestamp = get_current_timestamp();
                        int64_t msg_id = state->db_service.save_message(user, msg, timestamp);

                        json broadcast_json = {
                            {"type", "chat_message"},
                            {"id", msg_id},
                            {"status", msg_id != -1 ? "saved" : "db_error"},
                            {"username", user},
                            {"message", msg},
                            {"timestamp", timestamp}};

                        std::string log_line = "[#" + std::to_string(msg_id) + "] [" + timestamp + "] " + user + ": " + msg;
                        AppLog::info(log_line);

                        state->broadcast(broadcast_json.dump());
                    }
                    else
                    {
                        json error_json = {
                            {"type", "error"},
                            {"message", "Eksik alan: 'username' ve 'message' string olmalidir"}};
                        session->send(error_json.dump());
                    }
                }
                catch (const json::parse_error &pe)
                {
                    json error_json = {
                        {"type", "error"},
                        {"message", "Gecersiz JSON verisi"}};
                    session->send(error_json.dump());
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
            state->remove_session(session);
            AppLog::info("WebSocket oturumu kapandi, havuzdan cikarildi.");
        }
    }

}