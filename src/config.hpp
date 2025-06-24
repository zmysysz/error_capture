#pragma once
#include <toml++/toml.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <memory>

class config {
public:
    // global access to the config
    static std::shared_ptr<toml::table> get(const std::string& path = "config.toml") {
        static config instance(path);
        return instance.config_;
    }

    static void update(const std::string& path) {
    }

    config(const config&) = delete;
    config& operator=(const config&) = delete;

private:
    config(const std::string& path) {
        try {
            config_ = std::make_shared<toml::table>(toml::parse_file(path));
        } catch (const toml::parse_error& err) {
            std::cerr << "config: failed to parse: " << err.description() << "\n";
            throw std::runtime_error("config_parse_failed");
        }
    }

    std::shared_ptr<toml::table> config_;
};