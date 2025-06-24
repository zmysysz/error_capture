// log_capture.hpp
#pragma once

#include <string>
#include <vector>
#include <boost/regex.hpp>
#include <unordered_set>
#include <sstream>
#include <string_view>
#include "util.hpp"

namespace log_capture {

struct match_type {
    enum type {
        MATCH_LOGLEVEL,
        MATCH_KEYWORD,
        MATCH_EXPRESSION,
        MATCH_STACK_LINE,
        MATCH_WARNING_LOGLEVEL,
        MATCH_ERROR_FOLLOWING,
        MATCH_SKIP_LOGLEVEL,
        MATCH_EXCLUDE,
        MATCH_EXPRESSION_EXCLUDE,
        MATCH_RANGE_EXCLUDE_SATRT,
        MATCH_RANGE_EXCLUDE_END,
        MATCH_NONE
    };
    static std::string to_string(type t) {
        switch (t) {
        case MATCH_LOGLEVEL: return "MATCH_LOGLEVEL";
        case MATCH_KEYWORD: return "MATCH_KEYWORD";
        case MATCH_EXPRESSION: return "MATCH_EXPRESSION";
        case MATCH_STACK_LINE: return "MATCH_STACK_LINE";
        case MATCH_WARNING_LOGLEVEL: return "MATCH_WARNING_LOGLEVEL";
        case MATCH_ERROR_FOLLOWING: return "MATCH_ERROR_FOLLOWING";
        case MATCH_EXCLUDE: return "MATCH_EXCLUDE";
        case MATCH_SKIP_LOGLEVEL: return "MATCH_SKIP_LOGLEVEL";
        case MATCH_EXPRESSION_EXCLUDE: return "MATCH_EXPRESSION_EXCLUDE";
        case MATCH_RANGE_EXCLUDE_SATRT: return "MATCH_RANGE_EXCLUDE";
        case MATCH_RANGE_EXCLUDE_END: return "MATCH_RANGE_EXCLUDE_END";
        case MATCH_NONE: return "MATCH_NONE";
        default: return "UNKNOWN_MATCH_TYPE";
        }
    }
};

struct captrue_params {
    bool warn_cap = false; // whether to capture warning loglevel
    bool failed_log = false; // if the log explicitly failed/error log, then we can use reserve capture
    std::string format = "raw"; // raw or json format
};

struct capture_result {
    std::string line;
    size_t line_number;
    match_type::type type;
    std::string type_string;
};

struct range_match_pattern {
    boost::regex start_pattern;
    boost::regex end_pattern;
};

struct time_spend {
    struct value {
        double spend;
        int count;
    };
    class getter {
    public:
        getter(const std::string &name, std::map<std::string,value> &spends) :
            spends_(spends) {
            name_ = name;
            start_ = util::get_time();
        }
        ~getter() {
            double end = util::get_time();
            // add the spend to the spends_
            spends_[name_].spend += end - start_;
            spends_[name_].count++;
        }
    private:
        std::string name_;
        double start_;
        std::map<std::string,value> &spends_;
    };
    std::string to_string() {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        for (const auto& pair : spends) {
            ss << pair.first << ":"
               << "spend=" << pair.second.spend / 1000.
               << "ms,count=" << pair.second.count
               << ";";
        }
        return ss.str();
    }
    std::map<std::string,value> spends;
};

struct capture_context {
    captrue_params params;
    std::vector<capture_result> results; // all captured results
    time_spend spends; // time spend for each step
};

class capture {
public:
    capture();
    bool capture_from_file(const std::string &filepath, capture_context &cctx);
    bool capture_from_str(const std::string &lines, capture_context &cctx);
    //
    int load_expression_from_file(const std::string &filepath);
    int load_expression_exclude_from_file(const std::string &filepath);
    int load_keywords_from_file(const std::string &filepath);
    int load_keywords_exclude_from_file(const std::string &filepath);

private:
    match_type::type match_line(const std::string_view line, match_type::type last_captured,capture_context &cctx);
    match_type::type match_range_exclude(const std::string_view line, match_type::type last_captured, range_match_pattern & rangemp,capture_context &cctx);
    std::string_view remove_time_prefix(const std::string& line,capture_context &cctx);
    bool match_loglevel(const std::string_view line,capture_context &cctx);
    bool match_skip_loglevel(const std::string_view line,capture_context &cctx);
    bool match_warning_loglevel(const std::string_view line,capture_context &cctx);
    bool match_keyword(const std::string_view line,capture_context &cctx);
    bool match_expression(const std::string_view line,capture_context &cctx);
    bool match_exclude(const std::string_view line,capture_context &cctx);
    bool match_expression_exclude(const std::string_view line,capture_context &cctx);
    bool match_stack_line(const std::string_view line,capture_context &cctx);
    bool is_error_following(const std::string_view line,capture_context &cctx);
    bool if_log_failed(const std::string_view line);
    std::string_view get_substring(const std::string_view &line, size_t start, size_t size);
    bool is_matched(match_type::type type);
    std::string remove_ansi(const std::string& line,capture_context &cctx);
    void remove_inplace(std::string& line,capture_context &cctx);
    int getline(const std::string &lines, std::string &line, size_t &pos);
private:
    std::vector<boost::regex> time_patterns_;
    //
    boost::regex error_loglevel_pattern_;
    boost::regex skip_loglevel_pattern_;
    boost::regex warning_loglevel_pattern_;
    std::vector<boost::regex> stack_line_patterns_;

    std::unordered_set<std::string> keywords_;
    std::vector<boost::regex> expression_patterns_; 
    std::unordered_set<std::string> exclude_; // excluded exclude
    std::vector<boost::regex> expression_exclude_; // excluded exclude patterns
    std::vector<range_match_pattern> range_exclude_; //
    //
    boost::regex log_failed_flags_; // log failed flags
};
} // namespace log_capture
