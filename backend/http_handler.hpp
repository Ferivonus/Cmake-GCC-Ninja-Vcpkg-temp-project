#pragma once
#include <boost/beast/http.hpp>
#include <memory>
#include "backend/server_state.hpp"

namespace backend
{

    boost::beast::http::response<boost::beast::http::string_body> handle_http_request(
        boost::beast::http::request<boost::beast::http::string_body> &&req,
        std::shared_ptr<ServerState> state);

}