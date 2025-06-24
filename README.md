# error_capture
# Error Capture Project
A C++ project for log capture and analysis, designed to capture and filter error, warning, and exception information from logs. The project provides HTTP service interface for real-time log analysis and filtering.

## Core Features
1. Log Level Detection and Filtering
   
   - Error logs (ERROR, FATAL, PANIC, CRITICAL)
   - Warning logs (WARN, WARNING)
   - Filterable info logs (INFO, DEBUG, TRACE)
2. Stack Trace Analysis
   
   - Multi-language stack trace detection
   - Java/JS/C# stack traces
   - GDB & C++ stack information
   - Go stack traces
   - Python tracebacks
3. Keyword Matching
   
   - System errors (segfault, core dumped)
   - Connection errors (timeout, connection lost)
   - Resource errors (memory leak, too many)
   - Permission errors (not authorized, not permitted)
## Regular Expression Patterns
Many of the regex patterns used in this project are derived from Drain3 log parsing template training, including:

1. Time Patterns
   
   - ISO format: \[?\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(\.\d+)?((Z|z|[+-])\d{2}:?\d{2})?\]?
   - Apache format: \[?\d{2}/\w{3}/\d{4}:\d{2}:\d{2}:\d{2} [+-]\d{4}\]?
   - Common format: \[?\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}(\.\d+)?\]?
2. Stack Trace Patterns
   
   - Multiple language-specific patterns trained from real log data
   - Common stack trace headers and formats
   - Line number and file reference patterns
## API Documentation
### Log Capture API
```
POST /capture?warn_cap=false&
failed_log=false&format=raw
Content-Type: text/plain

Request Parameters (Query String):
- warn_cap: boolean    # Whether to 
capture warning logs
- failed_log: boolean  # If the log explicitly failed/error log, then we can use reserve capture
- format: string       # Output format: 'raw' or 'json'
- timestamp: # Timestamp for milliseconds

Request Body:
Raw log content (multiple lines)
```