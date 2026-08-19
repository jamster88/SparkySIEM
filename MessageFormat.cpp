/**
 * @file MessageFormat.cpp
 * @brief Implementation of the JSON message helpers.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#include "MessageFormat.h"

#include <chrono>   // Used for the system clock
#include <ctime>    // Used for localtime_r and strftime
#include <iomanip>  // Used for std::setfill and std::setw
#include <sstream>  // Used for std::ostringstream

namespace sparky {

std::string escapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);

    for (unsigned char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // Any other control character has to go out as \u00XX.
                    static const char* kHexDigits = "0123456789abcdef";
                    out += "\\u00";
                    out += kHexDigits[(c >> 4) & 0x0F];
                    out += kHexDigits[c & 0x0F];
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm localTime{};
    localtime_r(&nowTimeT, &localTime);  // thread safe, unlike std::localtime

    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);

    std::ostringstream timestamp;
    timestamp << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
    return timestamp.str();
}

std::string formatMessage(const std::string& filePath,
                          const std::string& line,
                          const std::string& topic,
                          const std::string& messageType,
                          const std::string& timestamp) {
    std::string message;
    message.reserve(line.size() + filePath.size() + 128);
    message += "{\"timestamp\": \"";
    message += escapeJson(timestamp);
    message += "\", \"filePath\": \"";
    message += escapeJson(filePath);
    message += "\", \"kafkaTopic\": \"";
    message += escapeJson(topic);
    message += "\", \"message\": \"";
    message += escapeJson(line);
    message += "\", \"type\": \"";
    message += escapeJson(messageType);
    message += "\"}";
    return message;
}

}  // namespace sparky
