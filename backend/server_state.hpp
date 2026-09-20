#pragma once
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include "config/app_config.hpp"
#include "services/json_service.hpp"
#include "services/db_service.hpp"
#include "services/crypto_service.hpp"

namespace backend
{
    class WsSession;

    struct ServerState
    {
        AppConfig config{"127.0.0.1", 8080, true};
        std::mutex mtx;
        JsonService json_service;
        DbService db_service;

        // Mesajları diskte AES-256 ile şifrelemek için türetilen anahtar
        std::vector<uint8_t> db_cipher_key{crypto::CryptoService::derive_key("SistemGuvendigiParola2026")};

        std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
        mutable std::mutex sessions_mtx;
        std::vector<std::shared_ptr<WsSession>> sessions;

        void add_session(std::shared_ptr<WsSession> session);
        void remove_session(std::shared_ptr<WsSession> session);
        void broadcast_to_room(int64_t room_id, const std::string &message);
        void evict_from_room(int64_t room_id);
        size_t active_session_count() const;
        size_t room_session_count(int64_t room_id) const;
    };
}