#pragma once

#include "libs/bst-http/bst_http.hpp"
#include <boost/asio.hpp>
#include <thread>
#include <memory>
#include <boost/asio/awaitable.hpp>
#include "log_capture.hpp"

namespace net = boost::asio;
namespace log_capture {
class request_handler : public std::enable_shared_from_this<request_handler>
{
public:
    request_handler(std::shared_ptr<log_capture::capture> cap);
    ~request_handler() = default;
    // Handle the request
    net::awaitable<void> hello(std::shared_ptr<http::request<http::string_body>> req,
        std::shared_ptr<http::response<http::string_body>> res,
        std::shared_ptr<bst::request_context> ctx);
    
    net::awaitable<void> capture(std::shared_ptr<http::request<http::string_body>> req,
        std::shared_ptr<http::response<http::string_body>> res,
        std::shared_ptr<bst::request_context> ctx);
private:
    std::shared_ptr<log_capture::capture> cap_ ;
};
} // namespace log_capture