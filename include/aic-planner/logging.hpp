#pragma once
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace aic
{
enum class LogLevel
{
    Error,
    Warning,
    Info,
    Debug
};
inline LogLevel log_level = LogLevel::Info;
inline LogLevel parse_log_level(const std::string &value)
{
    if (value == "error")
        return LogLevel::Error;
    if (value == "warning")
        return LogLevel::Warning;
    if (value == "info")
        return LogLevel::Info;
    if (value == "debug")
        return LogLevel::Debug;
    throw std::invalid_argument("Unknown log level: " + value);
}
template <class... Args> void log(LogLevel level, const Args &...args)
{
    if (static_cast<int>(level) > static_cast<int>(log_level))
        return;
    static const char *names[] = {"error", "warning", "info", "debug"};
    std::ostringstream message;
    (message << ... << args);
    std::cerr << '[' << names[static_cast<int>(level)] << "] " << message.str() << '\n';
}
} // namespace aic
