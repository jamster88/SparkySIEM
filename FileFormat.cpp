/**
 * @file FileFormat.cpp
 * @brief Implementation of pure formatting utilities for file change events.
 */

#include "FileFormat.h"

/**
 * @brief Escapes special characters in a string for valid JSON output.
 *
 * Handles: backslash, double quote, newline, carriage return, tab,
 * and other control characters (U+0000 to U+001F).
 */
static std::string jsonEscape(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 16); // slight over-alloc for escape chars

    for (char c : input) {
        switch (c) {
            case '\\': output += "\\\\"; break;
            case '"':  output += "\\\""; break;
            case '\n': output += "\\n";  break;
            case '\r': output += "\\r";  break;
            case '\t': output += "\\t";  break;
            case '\b': output += "\\b";  break;
            case '\f': output += "\\f";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    // Other control characters -> \u00XX
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    output += buf;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

/**
 * @brief Formats a file change event as a JSON string.
 */
std::string formatMessage(const std::string& filePath, const std::string& line,
                          const std::string& kafkaTopic, const std::string& messageType) {
    std::string timestamp = getCurrentTimestamp();

    // Escape all string fields for valid JSON
    std::string escFilePath   = jsonEscape(filePath);
    std::string escLine       = jsonEscape(line);
    std::string escKafkaTopic = jsonEscape(kafkaTopic);
    std::string escMessageType = jsonEscape(messageType);

    return "{\"timestamp\": \"" + timestamp + "\", \"filePath\": \"" + escFilePath + "\", "
           "\"kafkaTopic\": \"" + escKafkaTopic + "\", \"message\": \"" + escLine + "\", "
           "\"type\": \"" + escMessageType + "\"}";
}

/**
 * @brief Retrieves the current timestamp as a formatted string (UTC).
 */
std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[100];
    // Use gmtime for UTC (was localtime, changed for distributed-system compatibility)
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::gmtime(&now_c));

    std::ostringstream timestamp;
    timestamp << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
    return timestamp.str();
}
