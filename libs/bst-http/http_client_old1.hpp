// Core Boost.Beast/Asio HTTP client class with coroutine support
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <optional>
#include <system_error>
#include <iostream>
#include <variant>
#include "http_base.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
namespace ip = boost::asio::ip;
using tcp = boost::asio::ip::tcp;
using namespace std::chrono_literals;
using namespace asio::experimental::awaitable_operators;

namespace bst {

    // HTTP response structure
    struct response {
        int status_code;
        std::string body;
        http::fields headers;
        std::string version;
    };

    // HTTP request structure
    struct request {
        std::string method = "GET";
        std::optional<std::string> body;
        http::fields headers;
        int version = 11;
        bool keep_alive = true;
        bool receive_body = true;
    };

    // Parsed URL
    struct parsed_url {
        std::string host;
        std::string port;
        std::string target;
    };

    // Client configuration
    struct client_config {
        std::chrono::milliseconds connect_timeout = 5s;
        std::chrono::milliseconds request_timeout = 30s;
        std::chrono::milliseconds response_timeout = 30s;
        int max_retries = 3;
    };

    class co_http_request_impl {
    public:
        co_http_request_impl(asio::any_io_executor executor)
                    : _executor(executor)
                    , resolver_(executor)
                    , timer_(executor) {}

        ~co_http_request_impl() {
            if (stream_) {
                beast::error_code ec;
                stream_->socket().shutdown(tcp::socket::shutdown_both, ec);
                stream_->socket().close(ec);
            }
        }
        // Convenience methods

        asio::awaitable<int> co_get(const std::string &url, request &req, response &res) {
            req.method = "GET";
            return co_http_request(url, req, res);
        }

        asio::awaitable<int> co_post(const std::string& url, const std::string &body, request &req, response &res) {
            req.method = "POST";
            req.body = std::move(body);
            return co_http_request(url, req, res);
        }

        asio::awaitable<int> co_put(const std::string& url, const std::string &body, request &req, response &res) {
            req.method = "PUT";
            req.body = std::move(body);
            return co_http_request(url, req, res);
        }

        asio::awaitable<int> co_head(const std::string &url, request &req, response &res) {
            req.method = "HEAD";
            req.receive_body = false;
            return co_http_request(url, req, res);
        }

        // Set timeouts (ms)
        void set_conect_timeout(int timeout) { config_.connect_timeout = std::chrono::milliseconds(timeout); }
        void set_request_timeout(int timeout) { config_.request_timeout = std::chrono::milliseconds(timeout); }
        void set_response_timeout(int timeout) { config_.response_timeout = std::chrono::milliseconds(timeout); }

        // Set max retry count
        void set_max_retries(int max_retries) { config_.max_retries = max_retries; }

    public:
        asio::any_io_executor _executor;
        tcp::resolver resolver_;
        asio::steady_timer timer_;
        std::optional<beast::tcp_stream> stream_;
        client_config config_;
        parsed_url last_url_;
        

        // Perform an HTTP request
        asio::awaitable<int> co_http_request(const std::string &url, request &req, response &res)
        {
            parsed_url parsed = parse_url(url);
            auto status = co_await http_request(parsed.host, parsed.port, parsed.target, req, res);
            co_return status;
        }

        // Core HTTP request implementation
        asio::awaitable<int> http_request(
            const std::string& host,
            const std::string& port,
            const std::string& target,
            request &req,
            response &res) {

            int retry_count = 0;
            std::optional<response> last_response;
            std::exception_ptr last_exception;

            while (retry_count <= config_.max_retries) {
                try {
                    // Check if we can reuse an existing connection
                    if (req.keep_alive && stream_ &&
                        host == last_url_.host && port == last_url_.port &&
                        stream_->socket().is_open()) {
                        // Reuse existing connection
                    } else {
                        // Create a new connection
                        if (stream_) {
                            beast::error_code ec;
                            stream_->socket().shutdown(tcp::socket::shutdown_both, ec);
                            stream_->socket().close(ec);
                            stream_.reset(); // Reset stream
                            // Ignore errors, since connection may already be closed
                        }

                        // Resolve hostname
                        auto results = co_await resolver_.async_resolve(
                            host, port, asio::use_awaitable);

                        // Connect with timeout
                        co_await with_timeout(
                            config_.connect_timeout,
                            [this, &results]() -> asio::awaitable<void> {
                                return async_connect(results);
                            }());

                        last_url_.host = host;
                        last_url_.port = port;
                        last_url_.target = target;
                    }

                    // Prepare the request
                    http::request<http::string_body> bst_req;
                    bst_req.method_string(req.method);
                    bst_req.target(target);
                    bst_req.version(req.version);
                    bst_req.set(http::field::host, host);

                    // Set headers
                    for (const auto& header : req.headers) {
                        bst_req.set(header.name(), header.value());
                    }

                    // Set keep-alive
                    if (req.keep_alive && req.version == 11) {
                        bst_req.set(http::field::connection, "keep-alive");
                    } else {
                        bst_req.set(http::field::connection, "close");
                    }

                    // Set body
                    if (req.body) {
                        bst_req.body() = *req.body;
                        bst_req.prepare_payload();
                    }

                    // Send the request
                    co_await with_timeout(
                        config_.request_timeout,
                        [this, &bst_req]() -> asio::awaitable<void> {
                            co_await http::async_write(*stream_, bst_req, asio::use_awaitable);
                        }());

                    // Receive the response
                    http::response<http::string_body> bst_res;
                    beast::flat_buffer buffer;

                    co_await with_timeout(
                        config_.response_timeout,
                        [this, &bst_res, &buffer]() -> asio::awaitable<void> {
                            co_await http::async_read(*stream_, buffer, bst_res, asio::use_awaitable);
                        }());

                    // Clear body if not requested
                    if (!req.receive_body) {
                        bst_res.body().clear();
                    }

                    // Close connection if requested or required
                    if (bst_res.need_eof() || !req.keep_alive) {
                        beast::error_code ec;
                        stream_->socket().shutdown(tcp::socket::shutdown_both, ec);
                        stream_->socket().close(ec);
                        stream_.reset(); // Reset stream
                    }

                    // Return the response
                    res.status_code = bst_res.result_int();
                    res.body = std::move(bst_res.body());
                    res.headers = std::move(bst_res.base());
                    res.version = bst_res.version() == 10 ? "HTTP/1.0" : "HTTP/1.1";

                    co_return res.status_code;

                } catch (const std::exception& e) {
                    last_exception = std::current_exception();
                    if (stream_ && stream_->socket().is_open()) {
                        beast::error_code ec;
                        stream_->socket().shutdown(tcp::socket::shutdown_both, ec);
                        stream_->socket().close(ec);
                        stream_.reset(); // Reset stream
                    }

                    if (retry_count == config_.max_retries) {
                        std::rethrow_exception(last_exception);
                    }

                    retry_count++;
                    std::cerr << "Request failed (" << retry_count << "/" << config_.max_retries 
                              << "): " << e.what() << "\n";
                }
            }

            // All retries failed
            response error_res;
            error_res.status_code = -1; // Custom error code
            try {
                std::rethrow_exception(last_exception);
            } catch (const std::exception& e) {
                error_res.body = e.what();
            }
            co_return error_res.status_code;
        }

        // Asynchronous connect with timeout
        asio::awaitable<void> async_connect(const tcp::resolver::results_type& results) {
            stream_.emplace(_executor);
            co_await stream_->async_connect(results, asio::use_awaitable);

            // Set TCP options
            stream_->socket().set_option(tcp::no_delay(true));
        }

        // Coroutine wrapper with timeout
        template <typename T>
        asio::awaitable<T> with_timeout(std::chrono::milliseconds timeout, asio::awaitable<T> awaitable) {
            timer_.expires_after(timeout);
            auto result = co_await (std::move(awaitable) || timer_.async_wait(asio::use_awaitable));

            if (result.index() == 1) {
                // Timeout occurred
                timer_.cancel();
                throw std::runtime_error("Operation timed out");
            }

            // Return operation result
            if constexpr (!std::is_void_v<T>) {
                co_return std::get<0>(std::move(result));
            }
        }

        // URL parser
        parsed_url parse_url(std::string url) {
            parsed_url result;

            auto scheme_pos = url.find("://");
            if (scheme_pos != std::string::npos) {
                url = url.substr(scheme_pos + 3);
            }

            auto pos = url.find('/');
            if (pos == std::string::npos) {
                result.target = "/";
                pos = url.length();
            } else {
                result.target = url.substr(pos);
            }

            auto host_port = url.substr(0, pos);
            pos = host_port.find(':');
            if (pos == std::string::npos) {
                result.host = host_port;
                result.port = "80";
            } else {
                result.host = host_port.substr(0, pos);
                result.port = host_port.substr(pos + 1);
            }

            return std::move(result);
        }
    };
    // HTTP client class
    // Connection pool for managing multiple connections
    class connect_pool_bak {
    public:
        connect_pool_bak() = default;
        ~connect_pool_bak() = default;

        void add_connection(const std::string& host, const std::string& port, std::shared_ptr<beast::tcp_stream> stream) {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_[host + ":" + port].push_back(stream);
        }

        std::shared_ptr<beast::tcp_stream> get_connection(const std::string& host, const std::string& port) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = connections_.find(host + ":" + port);
            if (it != connections_.end() && !it->second.empty()) {
                auto stream = it->second.back();
                it->second.pop_back();
                return stream;
            }
            return nullptr;
        }
        void remove_connection(const std::string& host, const std::string& port, std::shared_ptr<beast::tcp_stream> stream) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = connections_.find(host + ":" + port);
            if (it != connections_.end()) {
                auto& conn_list = it->second;
                conn_list.erase(std::remove(conn_list.begin(), conn_list.end(), stream), conn_list.end());
            }
        }
        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.clear();
        }
        void clean_idle_connections() {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = connections_.begin(); it != connections_.end();) {
                if (it->second.empty()) {
                    it = connections_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    private:
        std::map<std::string, std::vector<std::shared_ptr<beast::tcp_stream>>> connections_;
        std::mutex mutex_;
    };

    class connect_pool {
        public:
            connect_pool() = default;
            ~connect_pool() = default;
        std::shared_ptr<co_http_request_impl> get_connection(const std::string& host, const std::string& port) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = connections_.find(host + ":" + port);
            if (it != connections_.end() && !it->second.empty()) {
                auto conn = it->second.back();
                it->second.pop_back();
                return conn;
            }
            return nullptr;
        }
        void add_connection(const std::string& host, const std::string& port, std::shared_ptr<co_http_request_impl> conn) {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_[host + ":" + port].push_back(conn);
        }
        void remove_connection(const std::string& host, const std::string& port, std::shared_ptr<co_http_request_impl> conn) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = connections_.find(host + ":" + port);
            if (it != connections_.end()) {
                auto& conn_list = it->second;
                conn_list.erase(std::remove(conn_list.begin(), conn_list.end(), conn), conn_list.end());
            }
        }
        void clear() {
            std::lock_guard<std::mutex> lock(mutex_);
            connections_.clear();
        }
        void clean_idle_connections() {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = connections_.begin(); it != connections_.end();) {
                if (it->second.empty()) {
                    it = connections_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        private:
            std::map<std::string, std::vector<std::shared_ptr<co_http_request_impl>>> connections_;
            std::mutex mutex_;
    };

    class connect_getter {
        public:
        connect_getter(const std::string &host,
            const std::string &port,
            std::shared_ptr<connect_pool> pool,
            asio::any_io_executor executor)
            : pool_(pool)
            ,connect_(nullptr)
            ,host_(host)
            ,port_(port) {
            connect_ = pool_->get_connection(host, port);
            if (!connect_) {
                connect_ = std::make_shared<co_http_request_impl>(executor);
            }
        }
        std::shared_ptr<co_http_request_impl> get() {
            return connect_;
        }
        ~connect_getter() {
            if (connect_) {
                pool_->add_connection(host_,port_,connect_);
            }
        }
        private:
        std::shared_ptr<co_http_request_impl> connect_;
        std::shared_ptr<connect_pool> pool_;
        std::string host_;
        std::string port_;
    };

    class global_io_pool {
        public:
            static global_io_pool& instance() {
                static global_io_pool inst;
                return inst;
            }
        
            asio::any_io_executor get_io_context() {
                return pool_.get_executor();
            }
        
            void join() {
                pool_.join();
            }
        
        private:
            global_io_pool() : pool_(std::thread::hardware_concurrency()) {}
            ~global_io_pool() = default;
        
            asio::thread_pool pool_;
    };

    // Synchronous interface wrapper
    class http_client {
    public:
        http_client()
            : pool_(std::make_shared<connect_pool>()) {}

        int get(const std::string &url, request &req, response &res) {
            req.method = "GET";
            return http_request(url, req, res);
        }
    private:
        std::shared_ptr<connect_pool> pool_;
        int http_request(const std::string &url, request &req, response &res)
        {
            parsed_url parsed = parse_url(url);
            asio::any_io_executor exec = global_io_pool::instance().get_io_context();
            connect_getter getter(parsed.host, parsed.port, pool_, exec);
            
            std::shared_ptr<co_http_request_impl> connect = getter.get();
            auto task = asio::co_spawn(exec, [&]() -> asio::awaitable<int> {              
                co_return co_await connect->co_http_request(url, req, res);
            }, asio::use_future);
            return task.get();  // Wait for coroutine to finish
        }

        parsed_url parse_url(std::string url) {
            parsed_url result;

            auto scheme_pos = url.find("://");
            if (scheme_pos != std::string::npos) {
                url = url.substr(scheme_pos + 3);
            }

            auto pos = url.find('/');
            if (pos == std::string::npos) {
                result.target = "/";
                pos = url.length();
            } else {
                result.target = url.substr(pos);
            }

            auto host_port = url.substr(0, pos);
            pos = host_port.find(':');
            if (pos == std::string::npos) {
                result.host = host_port;
                result.port = "80";
            } else {
                result.host = host_port.substr(0, pos);
                result.port = host_port.substr(pos + 1);
            }

            return std::move(result);
        }
    };

} // namespace bst
