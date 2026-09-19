// db_service.cpp
#include "services/db_service.hpp"
#include "config/app_config.hpp"

namespace backend
{
    DbService::DbService() = default;

    DbService::~DbService()
    {
        if (db_)
        {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }
    }

    bool DbService::init(const std::string &db_path)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);

        if (db_)
        {
            sqlite3_close_v2(db_);
            db_ = nullptr;
        }

        if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK)
        {
            AppLog::error("SQLite acilamadi: " + std::string(sqlite3_errmsg(db_)));
            if (db_)
            {
                sqlite3_close_v2(db_);
                db_ = nullptr;
            }
            return false;
        }

        sqlite3_busy_timeout(db_, 5000);
        sqlite3_exec(db_, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

        const char *schema = R"(
            CREATE TABLE IF NOT EXISTS rooms (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                is_open INTEGER NOT NULL DEFAULT 1,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS chat_messages (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                room_id INTEGER NOT NULL,
                username TEXT NOT NULL,
                message TEXT NOT NULL,
                created_at TEXT NOT NULL,
                FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE
            );
        )";

        char *err_msg = nullptr;
        if (sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg) != SQLITE_OK)
        {
            AppLog::error("Tablolar olusturulamadi: " + std::string(err_msg));
            sqlite3_free(err_msg);
            sqlite3_close_v2(db_);
            db_ = nullptr;
            return false;
        }

        AppLog::info("SQLite veritabani hazir (Oda sistemi ve WAL devrede): " + db_path);
        return true;
    }

    bool DbService::ping()
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return false;
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT 1;", -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        bool ok = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        return ok;
    }

    int64_t DbService::create_room(const std::string &name, const std::string &timestamp)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return -1;

        const char *sql = "INSERT INTO rooms (name, is_open, created_at, updated_at) VALUES (?, 1, ?, ?);";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return -1;

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, timestamp.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, timestamp.c_str(), -1, SQLITE_STATIC);

        int64_t inserted_id = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE)
        {
            inserted_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
        }
        sqlite3_finalize(stmt);
        return inserted_id;
    }

    std::vector<Room> DbService::get_rooms()
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        std::vector<Room> rooms;
        if (!db_)
            return rooms;

        const char *sql = "SELECT id, name, is_open, created_at, updated_at FROM rooms ORDER BY id ASC;";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return rooms;

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            Room r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            r.is_open = sqlite3_column_int(stmt, 2) == 1;
            r.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            r.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            rooms.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
        return rooms;
    }

    std::optional<Room> DbService::get_room(int64_t id)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return std::nullopt;

        const char *sql = "SELECT id, name, is_open, created_at, updated_at FROM rooms WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return std::nullopt;

        sqlite3_bind_int64(stmt, 1, id);

        std::optional<Room> res = std::nullopt;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            Room r;
            r.id = sqlite3_column_int64(stmt, 0);
            r.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
            r.is_open = sqlite3_column_int(stmt, 2) == 1;
            r.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            r.updated_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            res = std::move(r);
        }
        sqlite3_finalize(stmt);
        return res;
    }

    bool DbService::update_room(int64_t id, const std::string &name, bool is_open, const std::string &timestamp)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return false;

        const char *sql = "UPDATE rooms SET name = ?, is_open = ?, updated_at = ? WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, is_open ? 1 : 0);
        sqlite3_bind_text(stmt, 3, timestamp.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, id);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
        sqlite3_finalize(stmt);
        return success;
    }

    bool DbService::delete_room(int64_t id)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return false;

        const char *sql = "DELETE FROM rooms WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, id);
        bool success = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
        sqlite3_finalize(stmt);
        return success;
    }

    bool DbService::is_room_open(int64_t id)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return false;

        const char *sql = "SELECT is_open FROM rooms WHERE id = ?;";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        sqlite3_bind_int64(stmt, 1, id);
        bool open = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
        {
            open = (sqlite3_column_int(stmt, 0) == 1);
        }
        sqlite3_finalize(stmt);
        return open;
    }

    int64_t DbService::save_message(int64_t room_id, const std::string &username, const std::string &message, const std::string &timestamp)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        if (!db_)
            return -1;

        const char *sql = "INSERT INTO chat_messages (room_id, username, message, created_at) VALUES (?, ?, ?, ?);";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return -1;

        sqlite3_bind_int64(stmt, 1, room_id);
        sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, timestamp.c_str(), -1, SQLITE_STATIC);

        int64_t inserted_id = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE)
        {
            inserted_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
        }
        sqlite3_finalize(stmt);
        return inserted_id;
    }

    std::vector<ChatMessage> DbService::get_room_messages(int64_t room_id, int limit)
    {
        std::lock_guard<std::mutex> lock(db_mtx_);
        std::vector<ChatMessage> messages;
        if (!db_)
            return messages;

        const char *sql = "SELECT id, room_id, username, message, created_at FROM chat_messages WHERE room_id = ? ORDER BY id ASC LIMIT ?;";
        sqlite3_stmt *stmt = nullptr;

        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return messages;

        sqlite3_bind_int64(stmt, 1, room_id);
        sqlite3_bind_int(stmt, 2, limit);

        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            ChatMessage m;
            m.id = sqlite3_column_int64(stmt, 0);
            m.room_id = sqlite3_column_int64(stmt, 1);
            m.username = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
            m.message = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
            m.created_at = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
            messages.push_back(std::move(m));
        }
        sqlite3_finalize(stmt);
        return messages;
    }
}