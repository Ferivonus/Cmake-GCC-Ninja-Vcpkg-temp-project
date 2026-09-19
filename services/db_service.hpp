// db_service.hpp
#pragma once
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <cstdint>
#include <sqlite3.h>

namespace backend
{
    struct Room
    {
        int64_t id{0};
        std::string name;
        bool is_open{true};
        std::string created_at;
        std::string updated_at;
    };

    struct ChatMessage
    {
        int64_t id{0};
        int64_t room_id{0};
        std::string username;
        std::string message;
        std::string created_at;
    };

    class DbService
    {
    public:
        DbService();
        ~DbService();

        DbService(const DbService &) = delete;
        DbService &operator=(const DbService &) = delete;

        bool init(const std::string &db_path = "app_chat.db");
        bool ping();

        // Oda CRUD operasyonları
        int64_t create_room(const std::string &name, const std::string &timestamp);
        std::vector<Room> get_rooms();
        std::optional<Room> get_room(int64_t id);
        bool update_room(int64_t id, const std::string &name, bool is_open, const std::string &timestamp);
        bool delete_room(int64_t id);
        bool is_room_open(int64_t id);

        // Mesaj operasyonları
        int64_t save_message(int64_t room_id, const std::string &username, const std::string &message, const std::string &timestamp);
        std::vector<ChatMessage> get_room_messages(int64_t room_id, int limit = 100);

    private:
        sqlite3 *db_{nullptr};
        std::mutex db_mtx_;
    };
}