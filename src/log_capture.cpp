// log_capture.cpp
#include "log_capture.hpp"
#include <fstream>
#include <iostream>

using namespace std::string_literals;

namespace log_capture {

capture::capture()     
{
    time_patterns_ = {
        boost::regex(R"(^\[?\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(\.\d+)?((Z|z|[+-])\d{2}:?\d{2})?\]? ?)"),
        boost::regex(R"(^\[?\d{2}/\w{3}/\d{4}:\d{2}:\d{2}:\d{2} [+-]\d{4}\]? ?)"), //DD/Mon/YYYY:HH:MM:SS ±ZZZZ
        boost::regex(R"(^\[?\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}(\.\d+)?\]? ?)")
    };
    const std::string loglievel_pattern_str = 
            R"([ \[]{1})"s
            R"((ERROR|Error|\[error\]|\[E\]|e:)"s
            R"(|FATAL|\[fatal\]|Fatal)"s
            R"(|PANIC|\[panic\]|Panic)"s
            R"(|CRITICAL|EXCEPTION|FAILURE|致命|错误|异常|失败))"s
            R"([ \]\:]{1})"s;
    error_loglevel_pattern_ = boost::regex(loglievel_pattern_str);

    const std::string skip_loglevel_pattern_str = 
            R"([ \[]{1})"s
            R"((INFO|Info|\[info\]|\[I\])"s
            R"(|DEBUG|\[debug\]|Debug)"s
            R"(|TRACE|\[trace\]|VERBOSE|信息|调试|跟踪))"s
            R"([ \]\:]{1})"s;
    skip_loglevel_pattern_ = boost::regex(skip_loglevel_pattern_str);
    
    const std::string warning_loglevel_pattern_str = 
        R"([ \[]{1})"s
        R"((WARN|Warn|\[warn\]|\[W\])"s
        R"(|WARNING|Warning|\[warning\]|警告))"s
        R"([ \]\:]{1})"s;
    
    warning_loglevel_pattern_ = boost::regex(warning_loglevel_pattern_str);

    stack_line_patterns_ =  {
        boost::regex(R"(Traceback|Caused by:|Exception:|Error:|Stack trace|Exception in thread|panic:|Fatal error|goroutine \d+)"),
        //for java/js/c#
        boost::regex(R"(\s*at\s[^\n]*(\.java|\.cs|\.js|\.mjs|\.ts|\.jsx|\.tsx|more$)(:\d+)?(:\d+)?(\)|$))"),
        //GDB & c++ 
        boost::regex(R"((\s+#\d+\s[^\n]+in\s))"),
        boost::regex(R"((In file included)?\s*from\s[^\n]*(\.(h|hpp|hxx|h++|hh)|/c\+\+/\d+/))"),
        // Go
        boost::regex(R"((goroutine\s+\d+\s+\[.*?\]:|main\.main\(.*\)))"),
        // Python
        boost::regex(R"(\s*File\s+"[^"]+\.py",\s+line\s+\d+,)"),

        //boost::regex(R"((?:\s+at\s|\s+\d+:|\s+#\d+\s|\[\d+\]\s|\s*at\s|\s+\w+:\d+:|\.{3}\s\d+\smore$))") 
    };

    keywords_ = {
        " segfault", "segmentation fault", "core dumped", " unhandled", "stack trace",
        " invalid", "  failed", " FAILED", " Failed", " refused", " timeout", "connection lost", " missing", "too many",
        "memory leak",  " unreachable", " crash", " inaccessible", " error", " err ", " ERROR", " ERR", " Error",
        " unexpected", "container terminated", " refused", "RuntimeError", " not used",
        " undefined", " note:", "No such ", "not found", "not supported", "not implemented",
        "not available", "not allowed", "not permitted", "not authorized", "not recognized",
        "not configured", "not initialized", "not ready", "not reachable", "not responding",
        "not supported", "not valid", "not correct", "not compatible", "not sufficient",
        " cannot access", " cannot connect", " cannot find", " cannot open",
        " cannot read", " cannot write", " cannot execute", " cannot create", " cannot delete",
        " cannot modify", " cannot update", " cannot install", " cannot uninstall",
        " cannot load", " cannot unload", " cannot start", " cannot stop", " cannot restart",
        "Unable to", "Failed to", "Error while"
    };

    expression_patterns_ = {
        boost::regex(R"((exit status |exit code )[1-9][0-9]*)", boost::regex::icase)
    };

    exclude_ = {
    };

    expression_exclude_ = {
        boost::regex(R"(^(?:\s*|)zookeeper-[0-9][^ ]+$)"),
        boost::regex(R"(^\[Pipeline\] )"),
        boost::regex(R"(^checking for (.+?)\.\.\. (.+)$)"),
        boost::regex(R"(^-rw-r--r--\s*1\s*)"),
        boost::regex(R"(^mv -f [^ ]+ [^ ]+)"),
        boost::regex(R"(^libtool: compile: gcc)"),
        boost::regex(R"(^libtool: install: (?:[^ ]+\s*){2,})"),
        boost::regex(R"(^(?=.*\bcc|g++|gcc\b)(?=.*-std=)*(?=.*-O[0123])(?=.*-fPIC)*(?=.* -g )*)"),
        boost::regex(R"(^Completed.*with.*file\(s\).*remaining(?: \(calculating...\))?)"),
        boost::regex(R"(^make\[[0-9]\]: (?:Entering|Leaving) directory)"),
        boost::regex(R"(^tar: [^ ]* time stamp .* in the future)"),
        boost::regex(R"(^upload: [^ ]* to [^ ]*)"),
        boost::regex(R"(^\+)")
    };

    range_exclude_ = {
        {boost::regex(R"(^(?:\s*|)Agent [^ ]*-[0-9a-z]{5} )"), boost::regex(R"(^(?:\s*|)Running on [^ ]*-[0-9a-z]{5} )")},
    };

    const std::string log_failed_flags_str = 
        R"(([A-Za-z]+: FAILURE)"s
        R"(|[A-Za-z]+: ERROR)"s
        R"(|[A-Za-z]+: FATAL)"s
        R"(|[A-Za-z]+: CRITICAL)"s
        R"(|[A-Za-z]+: PANIC))"s;
    log_failed_flags_ = boost::regex(log_failed_flags_str);
}

int capture::load_expression_from_file(const std::string &filepath) {
    //dot not clear the vector, just append to it
    std::ifstream in(filepath);
    if (!in) {
        return 0;
    }
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        boost::regex pattern(line);
        expression_patterns_.emplace_back(pattern);
        count++;
    }
    in.close();
    return count;
}

int capture::load_expression_exclude_from_file(const std::string &filepath) {
    std::ifstream in(filepath);
    if (!in) {
        return 0;
    }
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        boost::regex pattern(line);
        expression_exclude_.emplace_back(pattern);
        count++;
    }
    in.close();
    return count;
}

int capture::load_keywords_from_file(const std::string &filepath) {
    std::ifstream in(filepath);
    if (!in) {
        return 0;
    }
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        // remove leading and trailing spaces
        line.erase(0, line.find_first_not_of("\r\n\t\""));
        line.erase(line.find_last_not_of("\r\n\t") + 1);
        keywords_.insert(line);
        count++;
    }
    in.close();
    return count;
}

int capture::load_keywords_exclude_from_file(const std::string &filepath) {
    std::ifstream in(filepath);
    if (!in) {
        return 0;
    }
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        // remove leading and trailing spaces
        line.erase(0, line.find_first_not_of("\r\n\t\""));
        line.erase(line.find_last_not_of("\r\n\t") + 1);
        exclude_.insert(line);
        count++;
    }
    in.close();
    return count;
}

std::string_view capture::remove_time_prefix(const std::string& line,capture_context &cctx) {
    time_spend::getter getter("remove_time_prefix",cctx.spends.spends);
    std::string_view view(line);
    // only check the first x characters for time patterns
    std::string_view prefix = get_substring(view, 0, 32);

    for (const auto& pattern : time_patterns_) {
        boost::match_results<std::string_view::const_iterator> res;
        if (boost::regex_search(prefix.begin(), prefix.end(), res, pattern, boost::match_continuous)) {
            size_t offset = res.position() + res.length();
            return view.substr(offset);
        }
    }
    return view;
}

bool capture::match_loglevel(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_loglevel",cctx.spends.spends);
    bool be_match = false;
    boost::match_results<std::string_view::const_iterator> res;
    try {
        be_match =  boost::regex_search(line.begin(), line.end(), res, error_loglevel_pattern_) && res.size() > 0;
        if (!be_match)
            be_match = line.size() > 1 && line[0] == 'E' && (line[1] == ' ' | line[1] == ':');
    }
    catch(const std::exception& e) {
        util::print_err("match_loglevel regex_search error: %s",e.what());
    }
    return be_match;
}

bool capture::match_skip_loglevel(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_skip_loglevel",cctx.spends.spends);
    bool be_match = false;
    boost::match_results<std::string_view::const_iterator> res;
    try {
        be_match = boost::regex_search(line.begin(),line.end(), res, skip_loglevel_pattern_) && res.size() > 0;
        if(!be_match) {
            be_match = line.size() > 1 && line[0] == 'I' && (line[1] == ' ' | line[1] == ':');
        }
    }
    catch(const std::exception& e) {
        util::print_err("match_skip_loglevel regex_search error: %s",e.what());
    }
    return be_match;
}

bool capture::match_warning_loglevel(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_warning_loglevel",cctx.spends.spends);
    bool be_match = false;
    boost::match_results<std::string_view::const_iterator> res;
    try {
        be_match = boost::regex_search(line.begin(), line.end(), res, warning_loglevel_pattern_) && res.size() > 0;
        if (!be_match) {
            be_match = line.size() > 1 && line[0] == 'W' && (line[1] == ' ' | line[1] == ':');
        }
    }
    catch(const std::exception& e) {
        util::print_err("match_warning_loglevel regex_search error: %s",e.what());
    }
    return be_match;
}

bool capture::match_keyword(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_keyword",cctx.spends.spends);
    for (const auto& word : keywords_) {
        if (line.find(word) != std::string::npos) return true;
    }
    return false;
}

bool capture::match_expression(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_expression",cctx.spends.spends);
    boost::match_results<std::string_view::const_iterator> res;
    for (const auto& pattern : expression_patterns_) {
        try {
            if (boost::regex_search(line.begin(), line.end(), res, pattern) && res.size() > 0) {
                return true;
            }
        }
        catch(const std::exception& e) {
            util::print_err("match_expression regex_search error: %s",e.what());
        }
    }
    return false;
}

bool capture::match_exclude(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_exclude",cctx.spends.spends);
    for (const auto& word : exclude_) {
        if (line.find(word) != std::string::npos) 
            return true;
    }
    return false;
}

bool capture::match_expression_exclude(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_expression_exclude",cctx.spends.spends);
    boost::match_results<std::string_view::const_iterator> res;
    for (const auto& pattern : expression_exclude_) {
        try
        {
            if (boost::regex_search(line.begin(), line.end(), res, pattern, boost::regex_constants::match_continuous) && res.size() > 0) {
                return true;
            }   
        } catch(const std::exception& e) {
            util::print_err("match_expression_exclude regex_search error: %s",e.what());
        }
    }
    return false;
}

bool capture::match_stack_line(const std::string_view line,capture_context &cctx) {
    time_spend::getter getter("match_stack_line",cctx.spends.spends);
    boost::match_results<std::string_view::const_iterator> res;
    for(const auto& stack_line_pattern : stack_line_patterns_) {
        try {
            if (boost::regex_search(line.begin(),line.end(), res, stack_line_pattern) && res.size() > 0) {
                return true;
            }
        } catch(const std::exception& e) {
            util::print_err("match_stack_line regex_search error: %s",e.what());
        }
    }
    return false;
}

bool capture::is_error_following(const std::string_view line,capture_context &cctx) {
   if(line.size() < 3) {
       return false;
   }
   // check if the line starts with a tab or three spaces
   if(line[0] == '\t' || (line[0] == ' ' && line[1] == ' ' && line[2] == ' ')) {
       return true;
   }
   return false;
}

match_type::type capture::match_range_exclude(const std::string_view line, 
    match_type::type last_captured, 
    range_match_pattern & rangemp,
    capture_context &cctx) {
    time_spend::getter getter("match_range_exclude",cctx.spends.spends);
    if (last_captured != match_type::MATCH_RANGE_EXCLUDE_SATRT) {
        for (const auto& range : range_exclude_) {
            boost::match_results<std::string_view::const_iterator> res;
            try {
                if (boost::regex_search(line.begin(), line.end(), res, range.start_pattern, boost::match_continuous) && res.size() > 0) {
                    rangemp = range;
                    return match_type::MATCH_RANGE_EXCLUDE_SATRT;
                }
            }
            catch(const std::exception& e) {
                util::print_err("match_range_exclude start regex_search error: %s",e.what());
            }
        }
    }
    else {
        boost::match_results<std::string_view::const_iterator> res;
        try {
            if (boost::regex_search(line.begin(), line.end(), res, rangemp.end_pattern,  boost::match_continuous) && res.size() > 0) {
                return match_type::MATCH_RANGE_EXCLUDE_END;
            }
            else {
                return match_type::MATCH_RANGE_EXCLUDE_SATRT;
            }
        }
        catch(const std::exception& e) {
            util::print_err("match_range_exclude end regex_search error: %s",e.what());
        }
    }
    return match_type::MATCH_NONE;
}

match_type::type capture::match_line(const std::string_view line, match_type::type last_captured, capture_context &cctx) {
    
    time_spend::getter getter("match_line",cctx.spends.spends);

    if (match_loglevel(line,cctx)) {
        return match_type::MATCH_LOGLEVEL;
    }

    if (cctx.params.warn_cap && match_warning_loglevel(line,cctx)) {
        return match_type::MATCH_WARNING_LOGLEVEL;
    }

    if (match_keyword(line,cctx)) {
        return match_type::MATCH_KEYWORD;
    }

    if (match_stack_line(line,cctx)) {
        return match_type::MATCH_STACK_LINE;
    }

    if (match_expression(line,cctx)) {
        return match_type::MATCH_EXPRESSION;
    }

    if (is_matched(last_captured) && is_error_following(line,cctx)) {
        return match_type::MATCH_ERROR_FOLLOWING;
    }

    if (match_skip_loglevel(line,cctx)) {
        return  match_type::MATCH_SKIP_LOGLEVEL;
    }

    if (match_exclude(line,cctx)) {
        return match_type::MATCH_EXCLUDE;
    }

    if (match_expression_exclude(line,cctx)) {
        return match_type::MATCH_EXPRESSION_EXCLUDE;
    }

    return match_type::MATCH_NONE;
}

bool capture::if_log_failed(const std::string_view line) {
    boost::match_results<std::string_view::const_iterator> res;
    return boost::regex_search(line.begin(), line.end(), res, log_failed_flags_) && res.size() > 0;
}

bool capture::capture_from_file(const std::string &filepath, capture_context &cctx) {
    std::vector<capture_result> none_results;
    std::vector<capture_result> &results = cctx.results;
    results.clear();
    results.reserve(100);
    none_results.reserve(1000);

    std::ifstream in(filepath);
    if (!in) {
        return false;
    }
    std::string line,oline;
    match_type::type captured = match_type::MATCH_NONE;
    int line_number = 1;
    range_match_pattern rangemp;
    while (std::getline(in, oline)) {
        line = remove_ansi(oline,cctx); // remove ansi escape codes
        remove_inplace(line,cctx); // remove binary characters
        std::string_view view =  remove_time_prefix(line,cctx); // remove time prefix if exists
        captured = match_range_exclude(view, captured, rangemp,cctx);
        if(captured == match_type::MATCH_NONE)
            captured = match_line(view, captured, cctx);
        if (is_matched(captured)) {
            results.emplace_back(line, line_number, captured, match_type::to_string(captured));
        }
        // put  none to none_lines
        if(captured == match_type::MATCH_NONE) {
            // if the line is not matched, we can still capture it
            none_results.emplace_back(line, line_number, captured, match_type::to_string(captured));
        }
        line_number++;
    }
    in.close();
    bool failed_log = cctx.params.failed_log;
    if(failed_log == false && !line.empty() && if_log_failed(line)) {
        // if the last line is a failed log, we can capture it
        failed_log = true;
    }

    if(results.size() == 0 && failed_log) {
            // if no results, we return the none_results to reduce the line number
            results.swap(none_results);
    }
    return true;
}

bool capture::capture_from_str(const std::string &lines, capture_context &cctx) {
    std::vector<capture_result> none_results;
    std::vector<capture_result> &results = cctx.results;
    results.clear();
    results.reserve(100);
    none_results.reserve(1000);
    size_t pos = 0;
    // read line by line
    std::string line,oline;
    match_type::type captured = match_type::MATCH_NONE;
    int line_number = 1;
    range_match_pattern rangemp;
    while (getline(lines, oline, pos)) {
        line = remove_ansi(oline,cctx); // remove ansi escape codes
        remove_inplace(line,cctx); // remove binary characters
        std::string_view view = remove_time_prefix(line,cctx); // remove time prefix if exists
        captured = match_range_exclude(view, captured, rangemp,cctx);
        if(captured == match_type::MATCH_NONE)
            captured = match_line(view, captured, cctx);
        if (is_matched(captured)) {
            results.emplace_back(line, line_number, captured, match_type::to_string(captured));
        }
        // put  none to none_lines
        if(captured == match_type::MATCH_NONE) {
            // if the line is not matched, we can still capture it
            none_results.emplace_back(line, line_number, captured, match_type::to_string(captured));
        }
        line_number++;
    }
    bool failed_log = cctx.params.failed_log;
    if(failed_log == false && !line.empty() && if_log_failed(line)) {
        // if the last line is a failed log, we can capture it
        failed_log = true;
    }

    if(results.size() == 0 && failed_log) {
            // if no results, we return the none_results to reduce the line number
            results.swap(none_results);
    }
    return true;
}

std::string_view capture::get_substring(const std::string_view &line, size_t start, size_t size) {
    if (start >= line.size()) {
        return std::string_view();
    }
    size_t sub_size = std::min(size, line.size() - start);
    return line.substr(start, sub_size);
}

bool capture::is_matched(match_type::type type){
    switch (type) {
        case match_type::MATCH_NONE:
        case match_type::MATCH_SKIP_LOGLEVEL:
        case match_type::MATCH_EXCLUDE:
        case match_type::MATCH_EXPRESSION_EXCLUDE:
        case match_type::MATCH_RANGE_EXCLUDE_SATRT:
        case match_type::MATCH_RANGE_EXCLUDE_END:
            return false;       
        default:
            return true;
    }
}

std::string capture::remove_ansi(const std::string& line,capture_context &cctx) {
    // Regular expression to remove ANSI escape codes
    time_spend::getter getter("remove_ansi",cctx.spends.spends);
    static const boost::regex ansi_regex(R"(\x1B\[[0-9;?]*[A-Za-z])");
    return boost::regex_replace(line, ansi_regex, "");
}

void capture::remove_inplace(std::string& line,capture_context &cctx) {
    //remove binnary characters
    time_spend::getter getter("remove_inplace",cctx.spends.spends);
    line.erase(std::remove_if(line.begin(), line.end(),
        [](char c) {
            return !(std::isprint(static_cast<unsigned char>(c)) || c == '\n' || c == '\r' || c == '\t');
        }), line.end());
}

int capture::getline(const std::string &lines, std::string &line, size_t &pos) {
    if (pos >= lines.size()) return 0;

    // Find the next line break character: \r or \n
    size_t line_end = lines.find('\n', pos);
    if (line_end == std::string::npos) {
        // Last line without a newline character
        line = lines.substr(pos);
        pos = lines.size();
        return line.size();
    }

    // Extract line content
    line = lines.substr(pos, line_end - pos);
    pos = line_end+1;
    return line.size();
}

}   // namespace log_capture