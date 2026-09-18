#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <memory>
#include "backend/server_state.hpp"

namespace backend
{

    void handle_websocket_session(
        boost::beast::tcp_stream stream,
        boost::beast::http::request<boost::beast::http::string_body> req,
        std::shared_ptr<ServerState> state);

}