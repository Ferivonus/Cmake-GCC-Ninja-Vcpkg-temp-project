#pragma once
#include <string>
#include <mutex>
#include <cstdint>
#include <sqlite3.h>

class DbService
{
public:
    DbService();
    ~DbService();

    DbService(const DbService &) = delete;
    DbService &operator=(const DbService &) = delete;

    bool init(const std::string &db_path = "app_chat.db");
    int64_t save_message(const std::string &username, const std::string &message, const std::string &timestamp);

private:
    sqlite3 *db_{nullptr};
    std::mutex db_mtx_;
};