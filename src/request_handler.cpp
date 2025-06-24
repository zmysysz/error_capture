#include "request_handler.hpp"
#include <nlohmann/json.hpp>
#include "util.hpp"

namespace net = boost::asio;

namespace log_capture {

    request_handler::request_handler(std::shared_ptr<log_capture::capture> cap) : 
            cap_(std::move(cap)) {
        // Constructor implementation if needed
    }

    net::awaitable<void> request_handler::hello(std::shared_ptr<http::request<http::string_body>> req,
            std::shared_ptr<http::response<http::string_body>> res,
            std::shared_ptr<bst::request_context> ctx) {
        res->body() = "Hello!!!";
        co_return;
    }

    net::awaitable<void> request_handler::capture(std::shared_ptr<http::request<http::string_body>> req,
        std::shared_ptr<http::response<http::string_body>> res,
        std::shared_ptr<bst::request_context> ctx) {
        // params check
        if (req->method() != http::verb::post) {
            res->result(http::status::bad_request);
            res->set(http::field::content_type, "text/plain");
            res->body() = "Method not allowed, only POST is allowed.";
            util::print_err("Method not allowed, url: %s",std::string(req->target()).c_str());
            co_return;
        }
        if (req->body().empty()) {
            res->result(http::status::bad_request);
            res->set(http::field::content_type, "text/plain");
            res->body() = "Request body is required.";
            util::print_err("Request body is required, url: %s",std::string(req->target()).c_str());
            co_return;
        }
        // request query string sure=1
        capture_context cap_ctx;
        auto p = ctx->get_param("failedlog");
        if (p == "1" || p == "true")
            cap_ctx.params.failed_log = true;
        
        p = ctx->get_param("warncap");
        if (p == "1" || p == "true")
            cap_ctx.params.warn_cap = true;
        
        p = ctx->get_param("format");
        if (p == "json") 
            cap_ctx.params.format = "json";
        
        // get the request body
        const std::string &body = req->body();
        if(!cap_->capture_from_str(body, cap_ctx)) {
            res->result(http::status::internal_server_error);
            res->set(http::field::content_type, "text/plain");
            res->body() = "Failed to capture logs from the request body.";
            util::print_err("Failed to capture logs from the request body, url: %s",std::string(req->target()).c_str());
            co_return;
        }
        // prepare the response body
        nlohmann::json json_response;
        json_response["status"] = "success";
        nlohmann::json line_json;
        std::string res_body;
        for (const auto& line : cap_ctx.results) {
            if(cap_ctx.params.format == "json") {
                // prepare json response
                line_json["line"] = line.line;
                line_json["linen"] = line.line_number;
                line_json["type"] = line.type_string;
                json_response["lines"].push_back(line_json);
            } else {
                res_body += line.line + "\n";
            }
        }
        if(cap_ctx.params.format == "json") {
            res_body = json_response.dump();
            res->set(http::field::content_type, "text/plain");
        } else {
            // if format is raw, we just return the raw lines
            res->set(http::field::content_type, "text/plain");
        }
        // prepare the response
        res->result(http::status::ok);
        res->body() = res_body;
        res->prepare_payload();
        util::print_info("Capture logs from the request body, url: %s, results: %d, spends: %s",
            std::string(req->target()).c_str(),
            cap_ctx.results.size(),
            cap_ctx.spends.to_string().c_str());
        co_return;
    }

} // namespace log_capture