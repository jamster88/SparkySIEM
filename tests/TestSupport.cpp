#include "TestSupport.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <cctype>
#include <stdexcept>

namespace testsupport {

std::size_t RecordingSink::countTypeLocked(const std::string& type) const {
    std::size_t n = 0;
    for (const std::string& message : messages) {
        std::string value;
        if (jsonField(message, "type", value) && value == type) {
            ++n;
        }
    }
    return n;
}

namespace {

/// Decodes the JSON string starting at json[pos] (which must be the opening
/// quote). On success, pos ends up just past the closing quote.
bool decodeString(const std::string& json, std::size_t& pos, std::string& out) {
    if (pos >= json.size() || json[pos] != '"') {
        return false;
    }
    ++pos;
    out.clear();

    while (pos < json.size()) {
        const char c = json[pos];
        if (c == '"') {
            ++pos;
            return true;
        }
        if (c == '\\') {
            if (pos + 1 >= json.size()) {
                return false;
            }
            const char esc = json[pos + 1];
            pos += 2;
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (pos + 4 > json.size()) {
                        return false;
                    }
                    const std::string hex = json.substr(pos, 4);
                    for (const char h : hex) {
                        if (!std::isxdigit(static_cast<unsigned char>(h))) {
                            return false;
                        }
                    }
                    pos += 4;
                    const long code = std::strtol(hex.c_str(), nullptr, 16);
                    // The monitor only ever emits \u for control characters, so a
                    // single byte is all that needs reconstructing here.
                    if (code > 0xff) {
                        return false;
                    }
                    out += static_cast<char>(code);
                    break;
                }
                default:
                    return false; // Not a legal JSON escape.
            }
            continue;
        }
        // A raw control character inside a string is invalid JSON.
        if (static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
        out += c;
        ++pos;
    }
    return false; // Unterminated string.
}

void skipSpaces(const std::string& json, std::size_t& pos) {
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
}

} // namespace

bool jsonField(const std::string& json, const std::string& key, std::string& out) {
    std::size_t pos = 0;
    skipSpaces(json, pos);
    if (pos >= json.size() || json[pos] != '{') {
        return false;
    }
    ++pos;

    while (pos < json.size()) {
        skipSpaces(json, pos);
        if (pos < json.size() && json[pos] == '}') {
            return false; // Ran out of fields without finding the key.
        }

        std::string name;
        if (!decodeString(json, pos, name)) {
            return false;
        }
        skipSpaces(json, pos);
        if (pos >= json.size() || json[pos] != ':') {
            return false;
        }
        ++pos;
        skipSpaces(json, pos);

        std::string value;
        if (!decodeString(json, pos, value)) {
            return false;
        }
        if (name == key) {
            out = value;
            return true;
        }

        skipSpaces(json, pos);
        if (pos < json.size() && json[pos] == ',') {
            ++pos;
        }
    }
    return false;
}

std::string field(const std::string& json, const std::string& key) {
    std::string value;
    return jsonField(json, key, value) ? value : std::string();
}

bool isWellFormedFlatJsonObject(const std::string& json) {
    std::size_t pos = 0;
    skipSpaces(json, pos);
    if (pos >= json.size() || json[pos] != '{') {
        return false;
    }
    ++pos;
    skipSpaces(json, pos);

    if (pos < json.size() && json[pos] == '}') {
        ++pos;
        skipSpaces(json, pos);
        return pos == json.size();
    }

    for (;;) {
        skipSpaces(json, pos);
        std::string name;
        if (!decodeString(json, pos, name)) {
            return false;
        }
        skipSpaces(json, pos);
        if (pos >= json.size() || json[pos] != ':') {
            return false;
        }
        ++pos;
        skipSpaces(json, pos);
        std::string value;
        if (!decodeString(json, pos, value)) {
            return false;
        }
        skipSpaces(json, pos);
        if (pos < json.size() && json[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < json.size() && json[pos] == '}') {
            ++pos;
            skipSpaces(json, pos);
            return pos == json.size();
        }
        return false;
    }
}

std::vector<std::string> ofType(const std::vector<std::string>& messages,
                                const std::string& type) {
    std::vector<std::string> out;
    for (const std::string& message : messages) {
        if (field(message, "type") == type) {
            out.push_back(message);
        }
    }
    return out;
}

std::vector<std::string> linesOfType(const std::vector<std::string>& messages,
                                     const std::string& type) {
    std::vector<std::string> out;
    for (const std::string& message : messages) {
        if (field(message, "type") == type) {
            out.push_back(field(message, "message"));
        }
    }
    return out;
}

TempDir::TempDir() {
    char templ[] = "/tmp/sparky_test_XXXXXX";
    const char* created = mkdtemp(templ);
    if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    dir = created;
}

TempDir::~TempDir() {
    // Shallow removal is enough: the tests only ever create plain files here.
    if (DIR* handle = opendir(dir.c_str())) {
        while (const struct dirent* entry = readdir(handle)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            const std::string full = dir + "/" + name;
            chmod(full.c_str(), 0644); // Undo any test that made a file unreadable.
            std::remove(full.c_str());
        }
        closedir(handle);
    }
    rmdir(dir.c_str());
}

void writeFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    out.flush();
}

void appendToFile(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << contents;
    out.flush();
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

MonitorHarness::MonitorHarness(const std::string& filePath, const std::string& topic) {
    auto owned = std::unique_ptr<RecordingSink>(new RecordingSink());
    recordingSink = owned.get();
    fileMonitor.reset(new FileMonitor(filePath, std::move(owned), topic));
}

MonitorHarness::~MonitorHarness() {
    stopAndJoin();
}

void MonitorHarness::start() {
    FileMonitor* monitor = fileMonitor.get();
    thread = std::thread([monitor] { monitor->monitor(); });
    // INIT and INIT - FILE OPEN are sent before the loop starts waiting; once we
    // have seen them the monitor is up and a test can act on the file.
    recordingSink->waitForCount(2);
}

void MonitorHarness::stopAndJoin() {
    if (joined) {
        return;
    }
    joined = true;
    if (thread.joinable()) {
        fileMonitor->stop();
        thread.join();
    }
}

std::size_t settleAndCount(RecordingSink& sink, std::chrono::milliseconds quietPeriod) {
    // Bounded, so a monitor that never stops producing returns a (large) count
    // for the test to fail on rather than hanging the suite.
    constexpr int kMaxRounds = 10;
    std::size_t previous = sink.count();
    for (int round = 0; round < kMaxRounds; ++round) {
        std::this_thread::sleep_for(quietPeriod);
        const std::size_t current = sink.count();
        if (current == previous) {
            return current;
        }
        previous = current;
    }
    return sink.count();
}

} // namespace testsupport
