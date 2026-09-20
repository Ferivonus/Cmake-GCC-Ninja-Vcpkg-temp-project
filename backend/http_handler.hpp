#pragma once
#include <boost/beast/http.hpp>
#include <memory>
#include "backend/server_state.hpp"

namespace backend
{
    /**
     * @brief İstemcilerden gelen HTTP REST isteklerini yönlendirir ve işler.
     *
     * Bu fonksiyon GET, POST, PUT, DELETE ve CORS için OPTIONS isteklerini karşılar;
     * /health, /api/rooms, /api/rooms/{id}/messages ve /api/config uç noktalarını yönetir.
     *
     * @param req İstemciden gelen taşınmış (moved) HTTP isteği
     * @param state Sunucunun paylaşılan durum nesnesi (veritabanı ve kripto anahtarları)
     * @return boost::beast::http::response<boost::beast::http::string_body> Üretilen HTTP yanıtı
     */
    boost::beast::http::response<boost::beast::http::string_body> handle_http_request(
        boost::beast::http::request<boost::beast::http::string_body> &&req,
        std::shared_ptr<ServerState> state);

} // namespace backend