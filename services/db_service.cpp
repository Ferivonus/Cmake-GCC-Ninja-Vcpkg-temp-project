#include "services/db_service.hpp"
#include "config/app_config.hpp"

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

    const char *schema = R"(
        CREATE TABLE IF NOT EXISTS chat_messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL,
            message TEXT NOT NULL,
            created_at TEXT NOT NULL
        );
    )";

    char *err_msg = nullptr;
    if (sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        AppLog::error("Tablo olusturulamadi: " + std::string(err_msg));
        sqlite3_free(err_msg);
        sqlite3_close_v2(db_);
        db_ = nullptr;
        return false;
    }

    AppLog::info("SQLite veritabani hazir (WAL modu devrede): " + db_path);
    return true;
}

int64_t DbService::save_message(const std::string &username, const std::string &message, const std::string &timestamp)
{
    std::lock_guard<std::mutex> lock(db_mtx_);
    if (!db_)
        return -1;

    const char *sql = "INSERT INTO chat_messages (username, message, created_at) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    {
        AppLog::error("Sorgu hazirlanamadi: " + std::string(sqlite3_errmsg(db_)));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, message.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, timestamp.c_str(), -1, SQLITE_STATIC);

    int64_t inserted_id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE)
    {
        inserted_id = static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
    }
    else
    {
        AppLog::error("Mesaj eklenemedi: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_finalize(stmt);
    return inserted_id;
}