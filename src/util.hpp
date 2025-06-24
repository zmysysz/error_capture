#pragma once
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdarg>
#include <vector>
#include <chrono>
namespace log_capture {

class util {
public:
    static double get_time() {
        return std::chrono::duration<double, std::micro>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    static void print_err(const char* format, ...) {
        va_list args;
        va_start(args, format);
        print_log_va(format,"ERROR", args);
        va_end(args);
    }
    static void print_info(const char* format, ...) {
        va_list args;
        va_start(args, format);
        print_log_va(format,"INFO", args);
        va_end(args);
    }
private:
    static void print_log_va(const char* format, const char* level, va_list args) {
        std::time_t t = std::time(nullptr);
        std::tm tm = *std::localtime(&t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        std::string time_str = ss.str();
        //
        char buffer[4096];
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        std::cerr << time_str << " [" << level << "] " << buffer << std::endl;
    }
};
} // namespace log_capture