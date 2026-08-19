#ifndef MESSAGE_H
#define MESSAGE_H

#include <string>

/**
 * @namespace sparky
 * @brief Pure helpers for building the JSON payloads that get shipped to Kafka.
 *
 * These are free functions rather than FileMonitor members so that they can be
 * exercised without a file, an inotify watch, or a Kafka producer, and so that
 * message formatting stays deterministic under test (the timestamp is passed in).
 */
namespace sparky {

/**
 * @brief Escapes a string so it is safe to embed in a JSON string literal.
 *
 * Handles the characters that JSON requires to be escaped: the double quote and
 * backslash, the shorthand escapes for backspace, form feed, newline, carriage
 * return and tab, and any remaining control character (below 0x20, plus 0x7f)
 * which is emitted as a \\u00XX sequence. Bytes at or above 0x80 are passed
 * through untouched so that valid UTF-8 input stays valid UTF-8 output.
 *
 * @param input The raw string, typically a line read from the monitored file.
 * @return The escaped string, without surrounding quotes.
 */
std::string jsonEscape(const std::string& input);

/**
 * @brief Formats a monitoring event as a single-line JSON object.
 *
 * Every field value is passed through jsonEscape(), so arbitrary log content -
 * including quotes, backslashes and tabs - produces parseable JSON.
 *
 * @param timestamp The event timestamp; pass currentTimestamp() in production.
 * @param filePath The path of the file being monitored.
 * @param topic The Kafka topic the message is destined for.
 * @param line The content of the line that triggered the event.
 * @param type The event type (e.g. "MODIFY", "ROTATE", "CLOSE").
 * @return A JSON object as a string, with no trailing newline.
 */
std::string formatMessage(const std::string& timestamp,
                          const std::string& filePath,
                          const std::string& topic,
                          const std::string& line,
                          const std::string& type);

/**
 * @brief Returns the current local time as "YYYY-MM-DD HH:MM:SS.mmm".
 *
 * Uses localtime_r() so the function is safe to call from multiple threads.
 *
 * @return A string containing the current timestamp in the format above.
 */
std::string currentTimestamp();

} // namespace sparky

#endif
