#ifndef MESSAGESINK_H
#define MESSAGESINK_H

#include <string>

/**
 * @class MessageSink
 * @brief Destination for formatted monitoring messages.
 *
 * FileMonitor talks to a MessageSink rather than to librdkafka directly. That
 * keeps the file-watching logic independent of the transport, which means the
 * monitor loop can be unit tested against a recording sink with no broker
 * involved. KafkaSink is the production implementation.
 *
 * Implementations are expected to be non-throwing on delivery problems: a
 * forwarder should report and count failures, not abort the monitor loop.
 */
class MessageSink {
public:
    virtual ~MessageSink() = default;

    /**
     * @brief Hands a fully formatted message to the destination.
     * @param message The message to deliver.
     */
    virtual void send(const std::string& message) = 0;

    /**
     * @brief Blocks until buffered messages are delivered, or the timeout expires.
     * @param timeoutMs Maximum time to wait, in milliseconds.
     */
    virtual void flush(int timeoutMs) = 0;
};

#endif
