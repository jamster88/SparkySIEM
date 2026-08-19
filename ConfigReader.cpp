/**
 * @file ConfigReader.cpp
 * @brief Placeholder for configuration file support. Not implemented yet.
 *
 * Broker, topic and monitored paths currently come from the command line, which
 * main.cpp parses directly:
 *
 *   ./SparkySIEM <broker> <topic> <path> [path...]
 *
 * The intent is for this to read the same settings from a file instead, so a deployed
 * forwarder can be configured without changing how it is launched, and eventually so
 * that configuration can be fetched from a central server. See the TO-DO in README.md.
 *
 * When this is filled in, the things worth reading from a config file are:
 * - the Kafka broker address, and later the TLS material for it
 * - the destination topic
 * - the list of files and directories to monitor
 * - the directory rescan interval, currently hardcoded as
 *   FilesMonitor::kDefaultScanInterval
 *
 * Nothing includes this file yet, and the Makefile does not compile it.
 *
 * @author Jamster88 (mcfadden@auburn.edu)
 * @date 4/15/25
 */
