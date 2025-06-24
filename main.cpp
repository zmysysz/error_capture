// main.cpp
#include "src/log_capture.hpp"
#include "src/request_handler.hpp"
#include <iostream>
#include <fstream>
#include <bst-http/bst_http.hpp>
#include "src/config.hpp"
#include <memory>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./error_capture cmd/server ...\n";
        return 1;
    }
    std::string type(argv[1]);
    if(type == "cmd" && argc < 3) {
        std::cerr << "Usage: ./error_capture cmd <filename>\n";
        return 1;
    }
    else if(type == "server" && argc < 2) {
        std::cerr << "Usage: ./error_capture server\n";
        return 1;
    }
    //load the capture parameters
    std::shared_ptr<log_capture::capture> cap = std::make_shared<log_capture::capture>();
    int exp_count = cap->load_expression_from_file("patterns/expression.txt");
    int exp_exclude_count = cap->load_expression_exclude_from_file("patterns/expression_exclude.txt");
    int keywords_count = cap->load_keywords_from_file("patterns/keywords.txt");
    int keywords_exclude_count = cap->load_keywords_exclude_from_file("patterns/keywords_exclude.txt");

    if(type == "cmd") {
        std::string filename(argv[2]);
        log_capture::capture_context context;
        cap->capture_from_file(filename,context);
        for (const auto& l : context.results) {
            std::cout << l.line << std::endl;
        }
    }
    else if (type == "server") {
        // Load the configuration
        auto cfg = config::get("config.toml");
        auto host = (*cfg)["server"]["host"].value_or("127.0.0.1");
        auto port = (*cfg)["server"]["port"].value_or(6565);
        auto thread_num = (*cfg)["server"]["thread"].value_or(4);
        // Print the configuration
        std::cout << "Server host: " << host << std::endl;
        std::cout << "Server port: " << port << std::endl;
        std::cout << "Server threads: " << thread_num << std::endl;
        std::cout << "Loaded patterns: " << exp_count << " expressions, "
                  << exp_exclude_count << " exclude expressions, "
                  << keywords_count << " keywords, "
                  << keywords_exclude_count << " exclude keywords." << std::endl;
        // Initialize the HTTP server
        bst::http_server server;
        server.init(host, port, thread_num);
        server.set_max_request_body_size(1000 * 1000 * 1000);
        // Register the request handlers
        std::shared_ptr<log_capture::request_handler> handler = std::make_shared<log_capture::request_handler>(cap);
        bst::request_handler::register_route("/hello", std::bind(&log_capture::request_handler::hello, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        bst::request_handler::register_route("/log/capture", std::bind(&log_capture::request_handler::capture, handler, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)); 
        std::cout << "Server start" << std::endl;
        server.run_server();
    }

    return 0;
}

