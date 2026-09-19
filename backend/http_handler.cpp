#include "backend/http_handler.hpp"
#include <nlohmann/json.hpp>
#include <regex>
#include <chrono>
#include <ctime>
#include <cstdint>

namespace backend
{
    namespace http = boost::beast::http;
    using json = nlohmann::json;

    static std::string current_time_str()
    {
        const auto now = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
#if defined(_WIN32)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        return std::string(buf);
    }

    static bool safe_parse_id(const std::string &str, int64_t &out_id)
    {
        try
        {
            size_t idx = 0;
            out_id = std::stoll(str, &idx);
            return idx == str.size();
        }
        catch (...)
        {
            return false;
        }
    }

    http::response<http::string_body> handle_http_request(
        http::request<http::string_body> &&req,
        std::shared_ptr<ServerState> state)
    {
        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::server, "ModernApp-Cloud/1.0");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(req.keep_alive());

        const std::string target = std::string(req.target());
        const auto q_pos = target.find('?');
        const std::string path = (q_pos != std::string::npos) ? target.substr(0, q_pos) : target;

        // 1. GET /health
        if (path == "/health")
        {
            if (req.method() != http::verb::get)
            {
                res.result(http::status::method_not_allowed);
                res.body() = R"({"error":"Sadece GET metodu desteklenir"})";
            }
            else
            {
                const auto now = std::chrono::steady_clock::now();
                const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - state->start_time).count();
                const bool db_alive = state->db_service.ping();

                const json health_payload = {
                    {"status", db_alive ? "healthy" : "degraded"},
                    {"uptime_seconds", uptime},
                    {"active_ws_connections", state->active_session_count()},
                    {"database", db_alive ? "connected" : "error"},
                    {"timestamp", current_time_str()}};

                res.result(db_alive ? http::status::ok : http::status::service_unavailable);
                res.body() = health_payload.dump();
            }
            res.prepare_payload();
            return res;
        }

        // 2. /api/rooms Koleksiyon Endpoint'leri
        if (path == "/api/rooms")
        {
            if (req.method() == http::verb::get)
            {
                const auto rooms = state->db_service.get_rooms();
                json rooms_arr = json::array();
                for (const auto &r : rooms)
                {
                    rooms_arr.push_back({{"id", r.id},
                                         {"name", r.name},
                                         {"is_open", r.is_open},
                                         {"active_ws_clients", state->room_session_count(r.id)},
                                         {"created_at", r.created_at},
                                         {"updated_at", r.updated_at}});
                }
                res.body() = json({{"rooms", rooms_arr}}).dump();
            }
            else if (req.method() == http::verb::post)
            {
                try
                {
                    const auto body_json = json::parse(req.body());
                    if (!body_json.is_object() || !body_json.contains("name") || !body_json["name"].is_string())
                    {
                        res.result(http::status::bad_request);
                        res.body() = R"({"error":"'name' alani string olarak zorunludur"})";
                    }
                    else
                    {
                        const std::string name = body_json["name"].get<std::string>();
                        if (name.empty())
                        {
                            res.result(http::status::bad_request);
                            res.body() = R"({"error":"Oda ismi bos olamaz"})";
                        }
                        else
                        {
                            const std::string ts = current_time_str();
                            const int64_t room_id = state->db_service.create_room(name, ts);

                            if (room_id != -1)
                            {
                                res.result(http::status::created);
                                res.body() = json({{"status", "success"},
                                                   {"room_id", room_id},
                                                   {"name", name},
                                                   {"is_open", true},
                                                   {"created_at", ts}})
                                                 .dump();

                                AppLog::info("[API] Yeni oda olusturuldu: '" + name + "' (ID: #" +
                                             std::to_string(room_id) + ")");
                            }
                            else
                            {
                                res.result(http::status::conflict);
                                // Delimiter çakışmasını önlemek için özel ayırıcı kullanıldı
                                res.body() = R"json({"error":"Oda olusturulamadi (isim cakismasi olabilir)"})json";
                            }
                        }
                    }
                }
                catch (const json::parse_error &)
                {
                    res.result(http::status::bad_request);
                    res.body() = R"({"error":"Gecersiz JSON"})";
                }
            }
            else
            {
                res.result(http::status::method_not_allowed);
                res.body() = R"({"error":"Metot desteklenmiyor"})";
            }
            res.prepare_payload();
            return res;
        }

        // 3. /api/rooms/{id}/messages Mesaj Geçmişi
        static const std::regex msg_regex(R"(^/api/rooms/(\d+)/messages$)");
        std::smatch match_msg;
        if (std::regex_match(path, match_msg, msg_regex))
        {
            if (req.method() == http::verb::get)
            {
                int64_t room_id = 0;
                if (!safe_parse_id(match_msg[1].str(), room_id))
                {
                    res.result(http::status::bad_request);
                    res.body() = R"({"error":"Gecersiz oda ID formati"})";
                }
                else
                {
                    const auto room = state->db_service.get_room(room_id);
                    if (!room)
                    {
                        res.result(http::status::not_found);
                        res.body() = R"({"error":"Oda bulunamadi"})";
                    }
                    else
                    {
                        const auto messages = state->db_service.get_room_messages(room_id);
                        json msg_arr = json::array();
                        for (const auto &m : messages)
                        {
                            msg_arr.push_back({{"id", m.id},
                                               {"room_id", m.room_id},
                                               {"username", m.username},
                                               {"message", m.message},
                                               {"created_at", m.created_at}});
                        }
                        res.body() = json({{"room_id", room_id}, {"messages", msg_arr}}).dump();
                    }
                }
            }
            else
            {
                res.result(http::status::method_not_allowed);
                res.body() = R"({"error":"Metot desteklenmiyor"})";
            }
            res.prepare_payload();
            return res;
        }

        // 4. /api/rooms/{id} Tekil Oda CRUD (GET, PUT, DELETE)
        static const std::regex room_regex(R"(^/api/rooms/(\d+)$)");
        std::smatch match_room;
        if (std::regex_match(path, match_room, room_regex))
        {
            int64_t room_id = 0;
            if (!safe_parse_id(match_room[1].str(), room_id))
            {
                res.result(http::status::bad_request);
                res.body() = R"({"error":"Gecersiz oda ID formati"})";
                res.prepare_payload();
                return res;
            }

            if (req.method() == http::verb::get)
            {
                const auto room = state->db_service.get_room(room_id);
                if (room)
                {
                    res.body() = json({{"id", room->id},
                                       {"name", room->name},
                                       {"is_open", room->is_open},
                                       {"active_ws_clients", state->room_session_count(room->id)},
                                       {"created_at", room->created_at},
                                       {"updated_at", room->updated_at}})
                                     .dump();
                }
                else
                {
                    res.result(http::status::not_found);
                    res.body() = R"({"error":"Oda bulunamadi"})";
                }
            }
            else if (req.method() == http::verb::put)
            {
                try
                {
                    const auto body_json = json::parse(req.body());
                    if (!body_json.is_object())
                    {
                        res.result(http::status::bad_request);
                        res.body() = R"({"error":"Gecersiz JSON govdesi"})";
                        res.prepare_payload();
                        return res;
                    }

                    const auto current = state->db_service.get_room(room_id);
                    if (!current)
                    {
                        res.result(http::status::not_found);
                        res.body() = R"({"error":"Guncellenecek oda bulunamadi"})";
                    }
                    else
                    {
                        std::string new_name = current->name;
                        if (body_json.contains("name"))
                        {
                            if (!body_json["name"].is_string() || body_json["name"].get<std::string>().empty())
                            {
                                res.result(http::status::bad_request);
                                res.body() = R"({"error":"'name' alani bos olmayan bir string olmalidir"})";
                                res.prepare_payload();
                                return res;
                            }
                            new_name = body_json["name"].get<std::string>();
                        }

                        bool new_open = current->is_open;
                        if (body_json.contains("is_open"))
                        {
                            if (!body_json["is_open"].is_boolean())
                            {
                                res.result(http::status::bad_request);
                                res.body() = R"({"error":"'is_open' alani boolean olmalidir"})";
                                res.prepare_payload();
                                return res;
                            }
                            new_open = body_json["is_open"].get<bool>();
                        }

                        const std::string ts = current_time_str();
                        if (state->db_service.update_room(room_id, new_name, new_open, ts))
                        {
                            if (!new_open && current->is_open)
                            {
                                const json close_evt = {
                                    {"type", "system"},
                                    {"event", "room_closed"},
                                    {"room_id", room_id},
                                    {"message", "Bu oda sunucu tarafindan kapatilmistir."}};
                                state->broadcast_to_room(room_id, close_evt.dump());
                                AppLog::info("[API] Oda kapatildi: '" + new_name + "' (ID: #" + std::to_string(room_id) + ")");
                            }
                            else if (new_open && !current->is_open)
                            {
                                AppLog::info("[API] Oda yeniden acildi: '" + new_name + "' (ID: #" + std::to_string(room_id) + ")");
                            }
                            else
                            {
                                AppLog::info("[API] Oda guncellendi: '" + new_name + "' (ID: #" + std::to_string(room_id) + ")");
                            }

                            res.body() = json({{"status", "success"},
                                               {"message", "Oda guncellendi"},
                                               {"room_id", room_id},
                                               {"name", new_name},
                                               {"is_open", new_open}})
                                             .dump();
                        }
                        else
                        {
                            res.result(http::status::internal_server_error);
                            res.body() = R"({"error":"Oda guncelleme basarisiz"})";
                        }
                    }
                }
                catch (const json::parse_error &)
                {
                    res.result(http::status::bad_request);
                    res.body() = R"({"error":"Gecersiz JSON"})";
                }
            }
            else if (req.method() == http::verb::delete_)
            {
                if (state->db_service.delete_room(room_id))
                {
                    const json del_evt = {
                        {"type", "system"},
                        {"event", "room_deleted"},
                        {"room_id", room_id},
                        {"message", "Oda silinmistir."}};
                    state->broadcast_to_room(room_id, del_evt.dump());
                    state->evict_from_room(room_id);

                    AppLog::info("[API] Oda silindi (ID: #" + std::to_string(room_id) + ")");

                    res.body() = json({{"status", "success"}, {"message", "Oda ve tum mesajlari silindi"}}).dump();
                }
                else
                {
                    res.result(http::status::not_found);
                    res.body() = R"({"error":"Oda bulunamadi veya silinemedi"})";
                }
            }
            else
            {
                res.result(http::status::method_not_allowed);
                res.body() = R"({"error":"Metot desteklenmiyor"})";
            }
            res.prepare_payload();
            return res;
        }

        // 5. /api/config
        if (path == "/api/config")
        {
            if (req.method() == http::verb::get)
            {
                std::lock_guard<std::mutex> lock(state->mtx);
                res.body() = state->json_service.serialize(state->config);
            }
            else if (req.method() == http::verb::post)
            {
                auto parsed = state->json_service.deserialize(req.body());
                if (parsed)
                {
                    std::lock_guard<std::mutex> lock(state->mtx);
                    state->config = *parsed;
                    res.body() = R"({"status":"success","message":"Ayar guncellendi"})";
                }
                else
                {
                    res.result(http::status::bad_request);
                    res.body() = R"({"status":"error","message":"Gecersiz JSON"})";
                }
            }
            else if (req.method() == http::verb::delete_)
            {
                std::lock_guard<std::mutex> lock(state->mtx);
                state->config = AppConfig{"127.0.0.1", 8080, false};
                res.body() = R"({"status":"success","message":"Ayar silindi/sifirlandi"})";
            }
            else
            {
                res.result(http::status::method_not_allowed);
                res.body() = R"({"error":"Izin verilmeyen metot"})";
            }
            res.prepare_payload();
            return res;
        }

        res.result(http::status::not_found);
        res.body() = R"({"error":"Endpoint bulunamadi"})";
        res.prepare_payload();
        return res;
    }
}