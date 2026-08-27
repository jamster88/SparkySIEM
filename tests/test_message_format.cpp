/**
 * @file test_message_format.cpp
 * @brief Unit tests for the JSON message helpers.
 *
 * The escaping tests exist because the original implementation concatenated raw file
 * content into a JSON string. A log line holding a quote or a backslash produced a
 * message no consumer could parse.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 */

#include <regex>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "MessageFormat.h"

namespace {

using sparky::escapeJson;
using sparky::formatMessage;

TEST(EscapeJson, LeavesOrdinaryTextUntouched) {
    EXPECT_EQ(escapeJson("eat 1681692777 elephant"), "eat 1681692777 elephant");
}

TEST(EscapeJson, HandlesEmptyInput) {
    EXPECT_EQ(escapeJson(""), "");
}

TEST(EscapeJson, EscapesDoubleQuotes) {
    EXPECT_EQ(escapeJson("he said \"hi\""), "he said \\\"hi\\\"");
}

TEST(EscapeJson, EscapesBackslashes) {
    EXPECT_EQ(escapeJson("C:\\logs\\app.log"), "C:\\\\logs\\\\app.log");
}

TEST(EscapeJson, EscapesWhitespaceControlCharacters) {
    EXPECT_EQ(escapeJson("a\nb"), "a\\nb");
    EXPECT_EQ(escapeJson("a\tb"), "a\\tb");
    EXPECT_EQ(escapeJson("a\rb"), "a\\rb");
    EXPECT_EQ(escapeJson("a\bb"), "a\\bb");
    EXPECT_EQ(escapeJson("a\fb"), "a\\fb");
}

TEST(EscapeJson, EscapesOtherControlCharactersAsUnicode) {
    EXPECT_EQ(escapeJson(std::string("a\x01""b")), "a\\u0001b");
    EXPECT_EQ(escapeJson(std::string("\x1f")), "\\u001f");
}

TEST(EscapeJson, PassesUtf8Through) {
    // Multi-byte sequences are all above 0x20, so they must survive unchanged.
    const std::string utf8 = "caf\xc3\xa9 \xe2\x9c\x93";
    EXPECT_EQ(escapeJson(utf8), utf8);
}

TEST(FormatMessage, ContainsEveryFieldWithTheGivenValues) {
    const std::string message =
        formatMessage("/var/log/app.log", "line one", "my-topic", "MODIFY", "2025-04-04 10:11:12.013");

    const auto parsed = nlohmann::json::parse(message);
    EXPECT_EQ(parsed.at("timestamp"), "2025-04-04 10:11:12.013");
    EXPECT_EQ(parsed.at("filePath"), "/var/log/app.log");
    EXPECT_EQ(parsed.at("kafkaTopic"), "my-topic");
    EXPECT_EQ(parsed.at("message"), "line one");
    EXPECT_EQ(parsed.at("type"), "MODIFY");
}

TEST(FormatMessage, ProducesValidJsonForContentWithQuotesAndBackslashes) {
    // This exact content produced an unparseable message before escaping was added.
    const std::string nasty = "he said \"hi\" \\ done";
    const std::string message =
        formatMessage("/data/watch.txt", nasty, "topic", "MODIFY", "2025-04-04 10:11:12.013");

    ASSERT_NO_THROW(nlohmann::json::parse(message));
    EXPECT_EQ(nlohmann::json::parse(message).at("message"), nasty);
}

TEST(FormatMessage, RoundTripsControlCharactersInContent) {
    const std::string nasty = "tab\there\nnewline\x01ctrl";
    const std::string message =
        formatMessage("/data/watch.txt", nasty, "topic", "MODIFY", "2025-04-04 10:11:12.013");

    ASSERT_NO_THROW(nlohmann::json::parse(message));
    EXPECT_EQ(nlohmann::json::parse(message).at("message"), nasty);
}

TEST(FormatMessage, EscapesTheFilePathAndTopicToo) {
    const std::string message = formatMessage(R"(/data/we"ird\path)", "body", R"(to"pic)",
                                              "MODIFY", "2025-04-04 10:11:12.013");

    ASSERT_NO_THROW(nlohmann::json::parse(message));
    const auto parsed = nlohmann::json::parse(message);
    EXPECT_EQ(parsed.at("filePath"), R"(/data/we"ird\path)");
    EXPECT_EQ(parsed.at("kafkaTopic"), R"(to"pic)");
}

TEST(CurrentTimestamp, MatchesTheDocumentedFormat) {
    const std::regex pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}$)");
    EXPECT_TRUE(std::regex_match(sparky::currentTimestamp(), pattern))
        << "got: " << sparky::currentTimestamp();
}

TEST(CurrentTimestamp, DoesNotGoBackwards) {
    const std::string first = sparky::currentTimestamp();
    const std::string second = sparky::currentTimestamp();
    EXPECT_LE(first, second);  // lexicographic order matches chronological order here
}

}  // namespace
