#include "Message.h"
#include "TestSupport.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>

using testsupport::field;
using testsupport::isWellFormedFlatJsonObject;
using testsupport::jsonField;

// ---------------------------------------------------------------------------
// jsonEscape
// ---------------------------------------------------------------------------

TEST(JsonEscape, LeavesOrdinaryTextAlone) {
    EXPECT_EQ(sparky::jsonEscape("make 12345 taco"), "make 12345 taco");
    EXPECT_EQ(sparky::jsonEscape(""), "");
}

TEST(JsonEscape, EscapesQuotesAndBackslashes) {
    EXPECT_EQ(sparky::jsonEscape("he said \"hello\""), "he said \\\"hello\\\"");
    EXPECT_EQ(sparky::jsonEscape("C:\\logs\\app"), "C:\\\\logs\\\\app");
    // A quote already preceded by a backslash must have both escaped, otherwise
    // the backslash would escape our added quote and truncate the field.
    EXPECT_EQ(sparky::jsonEscape("trailing\\"), "trailing\\\\");
}

TEST(JsonEscape, UsesShorthandForCommonControlCharacters) {
    EXPECT_EQ(sparky::jsonEscape("a\nb"), "a\\nb");
    EXPECT_EQ(sparky::jsonEscape("a\tb"), "a\\tb");
    EXPECT_EQ(sparky::jsonEscape("a\rb"), "a\\rb");
    EXPECT_EQ(sparky::jsonEscape("a\bb"), "a\\bb");
    EXPECT_EQ(sparky::jsonEscape("a\fb"), "a\\fb");
}

TEST(JsonEscape, UsesUnicodeEscapesForOtherControlCharacters) {
    EXPECT_EQ(sparky::jsonEscape(std::string("a\x01" "b")), "a\\u0001b");
    EXPECT_EQ(sparky::jsonEscape(std::string("\x1f")), "\\u001f");
    EXPECT_EQ(sparky::jsonEscape(std::string("\x7f")), "\\u007f");
    // NUL is a legitimate byte in a log line and must not terminate the field.
    EXPECT_EQ(sparky::jsonEscape(std::string("a\0b", 3)), "a\\u0000b");
}

TEST(JsonEscape, PassesUtf8Through) {
    // High bytes must not be sign-extended into bogus escapes.
    const std::string utf8 = "caf\xc3\xa9 \xe2\x9c\x93";
    EXPECT_EQ(sparky::jsonEscape(utf8), utf8);
}

// ---------------------------------------------------------------------------
// formatMessage
// ---------------------------------------------------------------------------

TEST(FormatMessage, ProducesTheExpectedJsonExactly) {
    const std::string message = sparky::formatMessage(
        "2026-08-18 12:34:56.789", "/var/log/app.log", "my-topic", "eat 42 fish", "MODIFY");

    EXPECT_EQ(message,
              "{\"timestamp\": \"2026-08-18 12:34:56.789\", "
              "\"filePath\": \"/var/log/app.log\", "
              "\"kafkaTopic\": \"my-topic\", "
              "\"message\": \"eat 42 fish\", "
              "\"type\": \"MODIFY\"}");
}

TEST(FormatMessage, ExposesAllFiveFields) {
    const std::string message = sparky::formatMessage("TS", "FP", "TP", "MSG", "TY");

    EXPECT_EQ(field(message, "timestamp"), "TS");
    EXPECT_EQ(field(message, "filePath"), "FP");
    EXPECT_EQ(field(message, "kafkaTopic"), "TP");
    EXPECT_EQ(field(message, "message"), "MSG");
    EXPECT_EQ(field(message, "type"), "TY");
}

TEST(FormatMessage, HostileContentStaysValidJsonAndRoundTrips) {
    // This is the exact shape of line that used to produce unparseable output.
    const std::string line = "he said \"hello\" \\ tab\there";
    const std::string message =
        sparky::formatMessage("2026-08-18 12:34:56.789", "/tmp/f.txt", "t", line, "MODIFY");

    EXPECT_TRUE(isWellFormedFlatJsonObject(message)) << message;
    EXPECT_EQ(field(message, "message"), line);
}

TEST(FormatMessage, EscapesEveryFieldNotJustTheContent) {
    const std::string message =
        sparky::formatMessage("ts", "/tmp/od\"d/\\path", "to\"pic", "line", "TY\"PE");

    EXPECT_TRUE(isWellFormedFlatJsonObject(message)) << message;
    EXPECT_EQ(field(message, "filePath"), "/tmp/od\"d/\\path");
    EXPECT_EQ(field(message, "kafkaTopic"), "to\"pic");
    EXPECT_EQ(field(message, "type"), "TY\"PE");
}

TEST(FormatMessage, AMultilinePayloadStaysOnOneLine) {
    const std::string message =
        sparky::formatMessage("ts", "/tmp/f", "t", "first\nsecond", "MODIFY");

    EXPECT_EQ(message.find('\n'), std::string::npos);
    EXPECT_TRUE(isWellFormedFlatJsonObject(message)) << message;
    EXPECT_EQ(field(message, "message"), "first\nsecond");
}

// ---------------------------------------------------------------------------
// currentTimestamp
// ---------------------------------------------------------------------------

TEST(CurrentTimestamp, MatchesTheDocumentedFormat) {
    const std::string timestamp = sparky::currentTimestamp();
    const std::regex pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}$)");
    EXPECT_TRUE(std::regex_match(timestamp, pattern)) << timestamp;
}

TEST(CurrentTimestamp, MillisecondsAreAlwaysThreeDigits) {
    // Sampling repeatedly catches a millisecond value below 100 being written
    // without its leading zeros, which would shorten the field.
    for (int i = 0; i < 200; ++i) {
        const std::string timestamp = sparky::currentTimestamp();
        ASSERT_EQ(timestamp.size(), 23u) << timestamp;
        ASSERT_EQ(timestamp[19], '.') << timestamp;
    }
}

TEST(CurrentTimestamp, IsUsableAsAMessageTimestamp) {
    const std::string message =
        sparky::formatMessage(sparky::currentTimestamp(), "/tmp/f", "t", "line", "MODIFY");
    EXPECT_TRUE(isWellFormedFlatJsonObject(message)) << message;
}

// ---------------------------------------------------------------------------
// The test helpers themselves - if the JSON reader is wrong, so is every
// assertion built on top of it.
// ---------------------------------------------------------------------------

TEST(TestSupportJson, RejectsUnescapedContent) {
    // What the old formatMessage produced for a line containing a quote.
    const std::string broken =
        "{\"message\": \"he said \"hello\"\", \"type\": \"MODIFY\"}";
    EXPECT_FALSE(isWellFormedFlatJsonObject(broken));
}

TEST(TestSupportJson, RejectsRawControlCharactersInStrings) {
    EXPECT_FALSE(isWellFormedFlatJsonObject("{\"message\": \"a\nb\"}"));
}

TEST(TestSupportJson, AcceptsAWellFormedObject) {
    EXPECT_TRUE(isWellFormedFlatJsonObject("{\"a\": \"1\", \"b\": \"x\\ny\"}"));
    EXPECT_TRUE(isWellFormedFlatJsonObject("{}"));
}

TEST(TestSupportJson, ReportsMissingFields) {
    std::string value;
    EXPECT_FALSE(jsonField("{\"a\": \"1\"}", "b", value));
}
