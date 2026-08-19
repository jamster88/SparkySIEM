/**
 * @file MessageSink.h
 * @brief Destination-agnostic interface for publishing monitor messages.
 *
 * FileMonitor and FilesMonitor talk to a MessageSink rather than to librdkafka
 * directly. In production the sink is a KafkaSink; in unit tests it is an in-memory
 * fake, which is what makes the monitors testable without a running broker.
 *
 * Implementations must be safe to call from several threads at once, because a
 * single sink is shared by every FileMonitor a FilesMonitor owns.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/4/25
 */

#ifndef MESSAGESINK_H
#define MESSAGESINK_H

#include <string>

/**
 * @brief Abstract destination for the messages produced by the monitors.
 */
class MessageSink {
public:
    virtual ~MessageSink() = default;

    /**
     * @brief Publishes one message.
     * @param message The fully formatted message to publish.
     * @throws std::runtime_error If the message could not be handed to the destination.
     */
    virtual void send(const std::string& message) = 0;

    /**
     * @brief Blocks until buffered messages have been delivered, or the timeout expires.
     * @param timeoutMs Maximum time to wait, in milliseconds.
     */
    virtual void flush(int timeoutMs) = 0;
};

#endif
