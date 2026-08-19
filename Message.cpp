#include "Message.h"

#include <chrono>    // Used for the system clock
#include <ctime>     // Used for localtime_r() and strftime()
#include <iomanip>   // Used for std::setfill and std::setw
#include <sstream>   // Used for std::ostringstream

namespace sparky {

/**
 * @brief Escapes a string for embedding in a JSON string literal.
 *
 * See Message.h for the escaping rules. The output is built with a reserve()
 * of the input length because the common case - a log line with nothing that
 * needs escaping - copies through 1:1.
 */
std::string jsonEscape(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (const char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default: {
                // Compare as unsigned so bytes >= 0x80 (UTF-8 continuation bytes)
                // are not treated as negative and mangled into \u sequences.
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || uc == 0x7f) {
                    std::ostringstream esc;
                    esc << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                        << static_cast<int>(uc);
                    out += esc.str();
                } else {
                    out += c;
                }
                break;
            }
        }
    }
    return out;
}

/**
 * @brief Formats a monitoring event as a single-line JSON object.
 */
std::string formatMessage(const std::string& timestamp,
                          const std::string& filePath,
                          const std::string& topic,
                          const std::string& line,
                          const std::string& type) {
    return "{\"timestamp\": \"" + jsonEscape(timestamp) +
           "\", \"filePath\": \"" + jsonEscape(filePath) +
           "\", \"kafkaTopic\": \"" + jsonEscape(topic) +
           "\", \"message\": \"" + jsonEscape(line) +
           "\", \"type\": \"" + jsonEscape(type) + "\"}";
}

/**
 * @brief Returns the current local time as "YYYY-MM-DD HH:MM:SS.mmm".
 */
std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto now_c = std::chrono::system_clock::to_time_t(now);
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm{};
    localtime_r(&now_c, &tm);

    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);

    std::ostringstream timestamp;
    timestamp << buffer << "." << std::setfill('0') << std::setw(3) << milliseconds.count();
    return timestamp.str();
}

} // namespace sparky
