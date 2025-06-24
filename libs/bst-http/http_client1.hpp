#pragma once
#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/unordered_map.hpp>
#include <iostream>
#include <regex>
#include <vector>
#include <memory>
#include "http_base.hpp"

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
using tcp = net::ip::tcp;

namespace bst
{
    class http_client
    {
    private:
        int connect_time_out_;
        int request_time_out_;
        int response_time_out_;
        int idle_time_out_;
        int max_redirects_;
        int max_retries_;
        int req_http_version_;
        tcp::resolver resolver_;
        ssl::context ssl_ctx_{ssl::context::tlsv12_client};

        // Connection cache for HTTP and HTTPS
        boost::unordered_map<std::string, std::unique_ptr<beast::tcp_stream>> http_connections_;
        boost::unordered_map<std::string, std::unique_ptr<beast::ssl_stream<beast::tcp_stream>>> https_connections_;

    public:
        http_client()
            : connect_time_out_(3),
              request_time_out_(100),
              response_time_out_(100),
              idle_time_out_(300),
              max_redirects_(5),
              max_retries_(3),
              req_http_version_(11),
              resolver_(*base::get_io_ctx()) // Initialize resolver with global io_context
        {
            ssl_ctx_.set_default_verify_paths();
            ssl_ctx_.set_verify_mode(ssl::verify_peer);
        }

        ~http_client() {}

        int Get(const std::string &url,
                std::string &res_body,
                std::vector<std::pair<std::string, std::string>> *req_headers = nullptr,
                std::vector<std::pair<std::string, std::string>> *res_headers = nullptr)
        {
            http::request<http::string_body> req{http::verb::get, "", req_http_version_};
            http::response<http::string_body> res;
            if (!prepare_request(url, req, req_headers))
                return -1;

            if (!request(req, res))
                return -1;

            res_body = res.body();
            postpare_response(res, res_headers);
            return res.result_int();
        }

        int Post(const std::string &url,
                 const std::string &req_body,
                 std::string &res_body,
                 std::vector<std::pair<std::string, std::string>> *req_headers = nullptr,
                 std::vector<std::pair<std::string, std::string>> *res_headers = nullptr)
        {
            http::request<http::string_body> req{http::verb::post, "", req_http_version_};
            http::response<http::string_body> res;
            if (!prepare_request(url, req, req_headers))
                return -1;

            req.body() = req_body;
            req.prepare_payload();

            if (!request(req, res))
                return -1;

            res_body = res.body();
            postpare_response(res, res_headers);
            return res.result_int();
        }

        int Put(const std::string &url,
                const std::string &req_body,
                std::string &res_body,
                std::vector<std::pair<std::string, std::string>> *req_headers = nullptr,
                std::vector<std::pair<std::string, std::string>> *res_headers = nullptr)
        {
            http::request<http::string_body> req{http::verb::put, "", req_http_version_};
            http::response<http::string_body> res;
            if (!prepare_request(url, req, req_headers))
                return -1;

            req.body() = req_body;
            req.prepare_payload();

            if (!request(req, res))
                return -1;

            res_body = res.body();
            postpare_response(res, res_headers);
            return res.result_int();
        }

        int Delete(const std::string &url,
                   std::string &res_body,
                   std::vector<std::pair<std::string, std::string>> *req_headers = nullptr,
                   std::vector<std::pair<std::string, std::string>> *res_headers = nullptr)
        {
            http::request<http::string_body> req{http::verb::delete_, "", req_http_version_};
            http::response<http::string_body> res;
            if (!prepare_request(url, req, req_headers))
                return -1;

            if (!request(req, res))
                return -1;

            res_body = res.body();
            postpare_response(res, res_headers);
            return res.result_int();
        }

        int Head(const std::string &url,
                 std::vector<std::pair<std::string, std::string>> *req_headers = nullptr,
                 std::vector<std::pair<std::string, std::string>> *res_headers = nullptr)
        {
            http::request<http::string_body> req{http::verb::head, "", req_http_version_};
            http::response<http::string_body> res;
            if (!prepare_request(url, req, req_headers))
                return -1;

            if (!request(req, res))
                return -1;

            postpare_response(res, res_headers);
            return res.result_int();
        }

        int Options(const std::string &url,
                    std::vector<std::pair<std::string, std::string>> *req_headers = nullptr,
                    std::vector<std::pair<std::string, std::string>> *res_headers = nullptr)
        {
            http::request<http::string_body> req{http::verb::options, "", req_http_version_};
            http::response<http::string_body> res;
            if (!prepare_request(url, req, req_headers))
                return -1;

            if (!request(req, res))
                return -1;

            postpare_response(res, res_headers);
            return res.result_int();
        }

        void set_connect_time_out(int seconds) { connect_time_out_ = seconds; }
        void set_request_time_out(int seconds) { request_time_out_ = seconds; }
        void set_response_time_out(int seconds) { response_time_out_ = seconds; }
        void set_idle_time_out(int seconds) { idle_time_out_ = seconds; }
        void set_max_redirects(int num) { max_redirects_ = num; }
        void set_max_retries(int num) { max_retries_ = num; }
        void set_req_http_version(int version) { req_http_version_ = version; }

    private:
        bool prepare_request(const std::string &url,
                             http::request<http::string_body> &req,
                             std::vector<std::pair<std::string, std::string>> *req_headers)
        {
            std::string host, port, target;
            if (!parse_url(url, host, port, target))
                return false;

            req.target(target);
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            if (req_headers)
            {
                for (const auto &header : *req_headers)
                {
                    req.set(header.first, header.second);
                }
            }
            return true;
        }

        void postpare_response(http::response<http::string_body> &res,
                               std::vector<std::pair<std::string, std::string>> *res_headers)
        {
            if (res_headers)
            {
                for (const auto &field : res)
                {
                    res_headers->emplace_back(field.name_string().to_string(), field.value().to_string());
                }
            }
        }

        bool request(http::request<http::string_body> &req,
                     http::response<http::string_body> &res)
        {
            net::io_context &ioc = *base::get_io_ctx(); // Use global io_context
            bool success = false;
            net::spawn(ioc, [this, &req, &res, &success](net::yield_context yield) {
                success = do_request(req, res, yield);
            });
            ioc.run(); // Run the io_context
            return success;
        }

        bool do_request(http::request<http::string_body> &req,
                        http::response<http::string_body> &res,
                        net::yield_context yield)
        {
            std::string host = req[http::field::host].to_string();
            std::string port = "8001"; // Default HTTP port
            bool is_https = false;

            // Extract port and schema from host
            size_t colon_pos = host.find(':');
            if (colon_pos != std::string::npos)
            {
                port = host.substr(colon_pos + 1);
                host = host.substr(0, colon_pos);
            }

            // Check if the request is HTTPS
            if (req.target().starts_with("https://"))
            {
                is_https = true;
                port = port.empty() ? "443" : port;
            }

            // Resolve host
            boost::system::error_code ec;
            if (ec)
            {
                std::cerr << "Resolve failed: " << ec.message() << std::endl;
                return false;
            }

            // Get or create connection
            if (is_https)
            {
                auto &stream = get_https_connection(host, port);
                return do_https_request(host,port,stream, req, res, yield);
            }
            else
            {
                auto &stream = get_http_connection(host, port);
                return do_http_request(host,port,stream, req, res, yield);
            }
        }

        bool do_http_request(const std::string &host,const std::string &port,
			     beast::tcp_stream &stream,
                             http::request<http::string_body> &req,
                             http::response<http::string_body> &res,
                             net::yield_context yield)
        {
            boost::system::error_code ec;
            // Set connection timeout
            net::steady_timer connect_timer(stream.get_executor());
            connect_timer.expires_after(std::chrono::seconds(connect_time_out_));
            connect_timer.async_wait([&stream](const boost::system::error_code &ec) {
                if (!ec)
                {
                    std::cerr << "Connection timeout!" << std::endl;
                    stream.socket().close();
                }
            });

            auto results = resolver_.async_resolve(host, port, yield[ec]);
            // Connect to host
            beast::get_lowest_layer(stream).async_connect(results, yield[ec]);
            if (ec)
            {
                std::cerr << "Connection failed: " << ec.message() << std::endl;
                return false;
            }
            connect_timer.cancel();

            // Set request timeout
            net::steady_timer write_timer(stream.get_executor());
            write_timer.expires_after(std::chrono::seconds(request_time_out_));
            write_timer.async_wait([&stream](const boost::system::error_code &ec) {
                if (!ec)
                {
                    std::cerr << "Write timeout!" << std::endl;
                    stream.socket().close();
                }
            });

            // Send request
            http::async_write(stream, req, yield[ec]);
            if (ec)
            {
                std::cerr << "Write failed: " << ec.message() << std::endl;
                return false;
            }
            write_timer.cancel();

            // Set response timeout
            net::steady_timer read_timer(stream.get_executor());
            read_timer.expires_after(std::chrono::seconds(response_time_out_));
            read_timer.async_wait([&stream](const boost::system::error_code &ec) {
                if (!ec)
                {
                    std::cerr << "Read timeout!" << std::endl;
                    stream.socket().close();
                }
            });

            // Receive response
            beast::flat_buffer buffer;
            http::async_read(stream, buffer, res, yield[ec]);
            if (ec)
            {
                std::cerr << "Read failed: " << ec.message() << std::endl;
                return false;
            }
            read_timer.cancel();

            return true;
        }

        bool do_https_request(const std::string &host,const std::string &port,
        		      beast::ssl_stream<beast::tcp_stream> &stream,
                              http::request<http::string_body> &req,
                              http::response<http::string_body> &res,
                              net::yield_context yield)
        {
            boost::system::error_code ec;
            // Set connection timeout
            net::steady_timer connect_timer(stream.get_executor());
            connect_timer.expires_after(std::chrono::seconds(connect_time_out_));
            connect_timer.async_wait([&stream](const boost::system::error_code &ec) {
                if (!ec)
                {
                    std::cerr << "Connection timeout!" << std::endl;
                    beast::get_lowest_layer(stream).close();
                }
            });

            auto results = resolver_.async_resolve(host, port, yield[ec]);
            // Connect to host
            beast::get_lowest_layer(stream).async_connect(results, yield[ec]);
            if (ec)
            {
                std::cerr << "Connection failed: " << ec.message() << std::endl;
                return false;
            }
            connect_timer.cancel();

            // Perform SSL handshake
            stream.async_handshake(ssl::stream_base::client, yield[ec]);
            if (ec)
            {
                std::cerr << "SSL handshake failed: " << ec.message() << std::endl;
                return false;
            }

            // Set request timeout
            net::steady_timer write_timer(stream.get_executor());
            write_timer.expires_after(std::chrono::seconds(request_time_out_));
            write_timer.async_wait([&stream](const boost::system::error_code &ec) {
                if (!ec)
                {
                    std::cerr << "Write timeout!" << std::endl;
                    beast::get_lowest_layer(stream).close();
                }
            });

            // Send request
            http::async_write(stream, req, yield[ec]);
            if (ec)
            {
                std::cerr << "Write failed: " << ec.message() << std::endl;
                return false;
            }
            write_timer.cancel();

            // Set response timeout
            net::steady_timer read_timer(stream.get_executor());
            read_timer.expires_after(std::chrono::seconds(response_time_out_));
            read_timer.async_wait([&stream](const boost::system::error_code &ec) {
                if (!ec)
                {
                    std::cerr << "Read timeout!" << std::endl;
                    beast::get_lowest_layer(stream).close();
                }
            });

            // Receive response
            beast::flat_buffer buffer;
            http::async_read(stream, buffer, res, yield[ec]);
            if (ec)
            {
                std::cerr << "Read failed: " << ec.message() << std::endl;
                return false;
            }
            read_timer.cancel();

            return true;
        }

        bool parse_url(const std::string &url, std::string &host, std::string &port, std::string &target)
        {
            std::regex url_regex(R"(^(http|https)://([^:/]+)(?::(\d+))?(/.*)?$)");
            std::smatch match;
            if (std::regex_match(url, match, url_regex))
            {
                std::string scheme = match[1];
                host = match[2];
                port = match[3].str().empty() ? (scheme == std::string("https") ? std::string("443") : std::string("80")) : match[3];
                target = match[4].str().empty() ? std::string("") : match[4];
                return true;
            }
            else
            {
                std::cerr << "Invalid URL format: " << url << std::endl;
                return false;
            }
        }

        beast::tcp_stream &get_http_connection(const std::string &host, const std::string &port)
        {
            auto key = host + ":" + port;
            auto it = http_connections_.find(key);
            if (it == http_connections_.end())
            {
                auto stream = std::make_unique<beast::tcp_stream>(*base::get_io_ctx()); // Use global io_context
                http_connections_[key] = std::move(stream);
                return *http_connections_[key];
            }
            else
            {
                return *it->second;
            }
        }

        beast::ssl_stream<beast::tcp_stream> &get_https_connection(const std::string &host, const std::string &port)
        {
            auto key = host + ":" + port;
            auto it = https_connections_.find(key);
            if (it == https_connections_.end())
            {
                auto stream = std::make_unique<beast::ssl_stream<beast::tcp_stream>>(*base::get_io_ctx(), ssl_ctx_); // Use global io_context
                https_connections_[key] = std::move(stream);
                return *https_connections_[key];
            }
            else
            {
                return *it->second;
            }
        }
    };
} // namespace bst
