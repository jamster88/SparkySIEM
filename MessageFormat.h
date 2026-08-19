/**
 * @file MessageFormat.h
 * @brief Pure helpers for building the JSON messages that SparkySIEM publishes.
 *
 * These functions are deliberately free of any Kafka or inotify state so that they
 * can be unit tested directly. Every string that ends up inside a JSON message must
 * pass through escapeJson(), otherwise a log line containing a quote or a backslash
 * produces a message no consumer can parse.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#ifndef MESSAGEFORMAT_H
#define MESSAGEFORMAT_H

#include <string>

namespace sparky {

/**
 * @brief Escapes a string so it can be embedded in a JSON string literal.
 *
 * Handles the two characters that terminate or escape a JSON string (`"` and `\`),
 * the shorthand escapes (`\b`, `\f`, `\n`, `\r`, `\t`), and any remaining control
 * character below 0x20, which is emitted as a `\u00XX` sequence. Bytes at or above
 * 0x20 are passed through untouched, so valid UTF-8 content survives unchanged.
 *
 * @param input The raw text to escape.
 * @return The escaped text, without the surrounding quotes.
 */
std::string escapeJson(const std::string& input);

/**
 * @brief Retrieves the current local time as "YYYY-MM-DD HH:MM:SS.mmm".
 *
 * Uses localtime_r so it is safe to call from several monitor threads at once.
 *
 * @return A string containing the current timestamp.
 */
std::string currentTimestamp();

/**
 * @brief Builds the JSON message published for a single event.
 *
 * The timestamp is passed in rather than read from the clock so that callers (and
 * tests) control it.
 *
 * @param filePath    The path of the file the event relates to.
 * @param line        The file content, or a placeholder for lifecycle events.
 * @param topic       The destination topic; echoed back in the message body.
 * @param messageType The event type, e.g. "INIT", "MODIFY", "ROTATED", "CLOSE".
 * @param timestamp   The timestamp string to embed.
 * @return A JSON object as a string. All five values are escaped.
 */
std::string formatMessage(const std::string& filePath,
                          const std::string& line,
                          const std::string& topic,
                          const std::string& messageType,
                          const std::string& timestamp);

}  // namespace sparky

#endif
