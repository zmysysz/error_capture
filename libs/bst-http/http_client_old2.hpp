#pragma once

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <list>
#include <chrono>
#include <mutex>
#include <optional>
#include <atomic>
#include "http_base.hpp"
#include <thread>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace bst {

    class client_io_pool {
        public:
            static client_io_pool& instance() {
                static client_io_pool inst;
                return inst;
            }
        
            net::any_io_executor get_io_context() {
                return pool_.get_executor();
            }
        
            void join() {
                pool_.join();
            }
        
        private:
            client_io_pool() : pool_(1) {}
            ~client_io_pool() = default;
        
            net::thread_pool pool_;
    };

    class http_client : public std::enable_shared_from_this<http_client> {
    public:
        struct request {
            std::string url;
            std::string body;
            http::fields headers;
        };

        struct response {
            int status = -1;
            std::string body;
            http::fields headers;
            std::string redirected_url;
        };

    static std::shared_ptr<http_client> create() {
        return std::shared_ptr<http_client>(new http_client());
    }
    
    private:
        explicit http_client() 
            :timer_(client_io_pool::instance().get_io_context()),
            connect_timeout_(5),
            request_timeout_(60),
            response_timeout_(60),
            idle_timeout_(120),
            max_redirects_(5),
            max_retries_(3),
            http_version_(11),
            request_id_(0) {
            start_connection_cleaner();
        }
    public:
        void set_connect_timeout(int seconds) { connect_timeout_ = seconds; }
        void set_request_timeout(int seconds) { request_timeout_ = seconds; }
        void set_response_timeout(int seconds) { response_timeout_ = seconds; }
        void set_idle_timeout(int seconds) { idle_timeout_ = seconds; }
        void set_max_redirects(int num) { max_redirects_ = num; }
        void set_max_retries(int num) { max_retries_ = num; }
        void set_http_version(int version) { http_version_ = version; }

        int get(request &req, response& res,net::yield_context yield) {
             do_request(http::verb::get,req, res,yield);
             return res.status;
        }

        int get(request &req, response& res) {
           return start_request(http::verb::get, req, res);
        }

        int post(request &req, response& res) {
            return start_request(http::verb::post, req, res);
        }

        int put(request &req, response& res) {
            return start_request(http::verb::put, req, res);
        }

        int del(request &req, response& res) {
            return start_request(http::verb::delete_, req, res);
        }

        int head(request &req, response& res) {
            return start_request(http::verb::head, req, res);
        }

        int options(request &req, response& res) {
            return start_request(http::verb::options, req, res);
        }

    private:
        struct connection {
            beast::tcp_stream stream;
            beast::flat_buffer buffer;
            std::chrono::steady_clock::time_point last_used;

            explicit connection(beast::tcp_stream&& s)
                : stream(std::move(s)) {}
        };

        struct async_context {
            http::verb method;
            request req;
            response& res;
            std::atomic<bool> completed{false};
            
            // Delete copy operations
            async_context(const async_context&) = delete;
            async_context& operator=(const async_context&) = delete;
            
            // Add move operations
            async_context(async_context&& other) noexcept
                : method(other.method),
                req(std::move(other.req)),
                res(other.res),
                completed(other.completed.load()) {}
                
            async_context& operator=(async_context&& other) noexcept {
                method = other.method;
                req = std::move(other.req);
                res = other.res;
                completed = other.completed.load();
                return *this;
            }
            
            async_context(http::verb m, request r, response& resp, bool comp = false) 
                : method(m), req(std::move(r)), res(resp), completed(comp) {}
        };

        net::io_context ioc_;
        net::steady_timer timer_;
        std::mutex mutex_;
        std::unordered_map<std::string, std::list<std::shared_ptr<connection>>> conn_pool_;
        std::unordered_map<int, std::shared_ptr<async_context>> async_requests_;
        std::atomic<int> request_id_;

        int connect_timeout_;
        int request_timeout_;
        int response_timeout_;
        int idle_timeout_;
        int max_redirects_;
        int max_retries_;
        int http_version_;

        int start_request(http::verb method, request &req, response& res) {
        
            std::promise<int> promise;
            std::future<int> future = promise.get_future();
            net::spawn(client_io_pool::instance().get_io_context(), std::bind(&http_client::do_request,
                shared_from_this(), std::ref(method), 
                std::ref(req), 
                std::ref(res), 
                std::placeholders::_1), 
                [&](std::exception_ptr ex) {
                    if (ex) {
                        try {
                            std::rethrow_exception(ex);
                        } catch (const std::exception& e) {
                            std::cerr << "Error: " << e.what() << std::endl;
                        }
                        promise.set_value(-1);  // 或 set_exception
                    } else {
                        promise.set_value(res.status);
                    }
            });
            //client_io_pool::instance().join();
            return future.get();
        }
        
        void do_request(http::verb method,
                    request& req,
                    response& res,
                    net::yield_context yield) {
            int redirect_count = 0;
            int retry_count = 0;
            std::string current_url = req.url;
            while (true) {
                auto tmp_res = perform_request(method, req, current_url, yield);
                
                if (is_redirect(tmp_res.status) && redirect_count < max_redirects_) {
                    if (auto location = tmp_res.headers.find("Location"); location != tmp_res.headers.end()) {
                        current_url = std::string(location->value());
                        redirect_count++;
                        continue;
                    }
                }

                if (should_retry(tmp_res.status) && retry_count < max_retries_) {
                    retry_count++;
                    continue;
                }

                res = std::move(tmp_res);
                res.redirected_url = current_url;
                break;
            }
        }

        response perform_request(http::verb method,
                                const request& req,
                                const std::string& url,
                                net::yield_context yield) {
            try {
                auto parsed = parse_url(url);
                if (!parsed) {
                    return response{-2, "Invalid URL", {}, url};
                }

                auto conn = get_connection(parsed->host, parsed->port, yield);
                conn->stream.expires_after(std::chrono::seconds(connect_timeout_));

                http::request<http::string_body> http_req{method, parsed->target, http_version_};
                http_req.set(http::field::host, parsed->host);
                http_req.set(http::field::user_agent, "Boost.Beast");
                for (const auto& header : req.headers) {
                    http_req.set(header.name(), header.value());
                }
                http_req.body() = req.body;
                http_req.prepare_payload();

                conn->stream.expires_after(std::chrono::seconds(request_timeout_));
                http::async_write(conn->stream, http_req, yield);

                conn->stream.expires_after(std::chrono::seconds(response_timeout_));
                http::response<http::string_body> http_res;
                http::async_read(conn->stream, conn->buffer, http_res, yield);

                return_connection(std::move(conn), parsed->host, parsed->port);

                return response{
                    static_cast<int>(http_res.result_int()),
                    std::move(http_res.body()),
                    std::move(http_res.base()),
                    url};
            } catch (const std::exception& e) {
                return response{-1, e.what(), {}, url};
            }
        }

        std::shared_ptr<connection> get_connection(
            const std::string& host, const std::string& port,
            net::yield_context yield) {
            std::shared_ptr<connection> conn;

            {
                std::lock_guard lock(mutex_);
                auto& conn_list = conn_pool_[host + ":" + port];
                if (!conn_list.empty()) {
                    conn = std::move(conn_list.front());
                    conn_list.pop_front();
                }
            }

            if (!conn) {
                tcp::resolver resolver(client_io_pool::instance().get_io_context());
                auto endpoints = resolver.async_resolve(host, port, yield);

                beast::tcp_stream stream(client_io_pool::instance().get_io_context());
                stream.expires_after(std::chrono::seconds(connect_timeout_));
                boost::asio::async_connect(stream.socket(), endpoints, yield);

                conn = std::make_unique<connection>(std::move(stream));
            }

            conn->last_used = std::chrono::steady_clock::now();
            return conn;
        }

        void return_connection(std::shared_ptr<connection> conn, 
                            const std::string& host, const std::string& port) {
            std::lock_guard lock(mutex_);
            conn_pool_[host + ":" + port].push_back(std::move(conn));
        }

        void start_connection_cleaner() {
            timer_.expires_after(std::chrono::seconds(idle_timeout_));
            timer_.async_wait([this](boost::system::error_code ec) {
                if (!ec) {
                    clean_idle_connections();
                    start_connection_cleaner();
                }
            });
        }

        void clean_idle_connections() {
            auto now = std::chrono::steady_clock::now();
            std::lock_guard lock(mutex_);

            for (auto& [key, conn_list] : conn_pool_) {
                for (auto it = conn_list.begin(); it != conn_list.end(); ) {
                    if (now - (*it)->last_used > std::chrono::seconds(idle_timeout_)) {
                        it = conn_list.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        }

        bool is_redirect(int status) const {
            return status == 301 || status == 302 || status == 303 || 
                status == 307 || status == 308;
        }

        bool should_retry(int status) const {
            return status == -1 || status == 408 || status == 429 || status >= 500;
        }

        struct parsed_url {
            std::string host;
            std::string port;
            std::string target;
        };

        static std::optional<parsed_url> parse_url(std::string url) {
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
            
            return result;
        }
    };

} // namespace bst
