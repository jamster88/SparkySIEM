/**
 * @file FileFormat.h
 * @brief Pure utility functions for file change event formatting.
 *
 * Extracted from FileMonitor to enable testing without Kafka/inotify dependencies.
 * All functions in this header are platform-independent and can be tested on macOS/Linux.
 */

#ifndef FILEFORMAT_H
#define FILEFORMAT_H

#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

/**
 * @brief Formats a file change event as a JSON string.
 *
 * Produces a JSON object with timestamp, filePath, kafkaTopic, message (line content), and type.
 * Handles escaping of special characters (", \, newline, tab) in all string fields to produce valid JSON.
 *
 * @param filePath The path of the file being monitored.
 * @param line The content or line of text from the file.
 * @param kafkaTopic The Kafka topic this event relates to.
 * @param messageType The type of message (e.g., "MODIFY", "INIT", "CLOSE").
 * @return A valid JSON-formatted string.
 */
std::string formatMessage(const std::string& filePath, const std::string& line,
                          const std::string& kafkaTopic, const std::string& messageType);

/**
 * @brief Retrieves the current timestamp in a formatted string (UTC).
 *
 * Format: "YYYY-MM-DD HH:MM:SS.mmm"
 * Uses UTC to ensure consistent timestamps across distributed systems.
 *
 * @return A string representing the current UTC timestamp with millisecond precision.
 */
std::string getCurrentTimestamp();

#endif
