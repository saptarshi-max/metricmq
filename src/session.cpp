#if defined(_WIN32) || defined(WIN32)
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close_socket closesocket
#define sleep_ms(ms) Sleep(ms)
#define SOCKLEN_T int
#else
#include <unistd.h>
#include <arpa/inet.h>
#define close_socket close
#define sleep_ms(ms) usleep(ms * 1000)
#define SOCKLEN_T socklen_t
#endif

#include "session.hpp"
#include "broker.hpp"
#include "resp_parser.hpp"
#include "binary_protocol.hpp"
#include "metricmq/logger.hpp"
#include "metricmq/crypto.hpp"
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <algorithm>

namespace metricmq {

Session::Session(int sock_fd, Broker* broker)
    : sock_fd_(sock_fd), broker_(broker), protocol_type_(ProtocolType::UNKNOWN), sequence_(0) {
    LOG_DEBUG("Session created: fd={}", sock_fd);
    std::cout << "New client connected: " << sock_fd << "\n";
}

ProtocolType Session::detectProtocol(const std::string& buffer) {
    if (buffer.empty()) return ProtocolType::UNKNOWN;
    
    uint8_t first_byte = static_cast<uint8_t>(buffer[0]);
    
    // RESP starts with: +, -, :, $, *
    if (first_byte == '+' || first_byte == '-' || first_byte == ':' ||
        first_byte == '$' || first_byte == '*') {
        LOG_DEBUG("Protocol detected: RESP (fd={})", sock_fd_);
        return ProtocolType::RESP;
    }
    
    // Binary protocol: version byte (currently 0x01)
    if (first_byte == BINARY_PROTOCOL_VERSION) {
        LOG_DEBUG("Protocol detected: BINARY (fd={})", sock_fd_);
        return ProtocolType::BINARY;
    }
    
    LOG_WARN("Unknown protocol detected: first_byte=0x{:02x} (fd={})", first_byte, sock_fd_);
    return ProtocolType::UNKNOWN;
}

void Session::run() {
    char buffer[4096];
    while (true) {
        int n = recv(sock_fd_, buffer, sizeof(buffer), 0);
        if (n < 0) {
            std::cerr << "Recv error\n";
            break;
        }
        if (n == 0) {
            break;  // connection closed gracefully
        }

        // Append to receive buffer
        recv_buffer_.append(buffer, n);

        // Auto-detect protocol on first message
        if (protocol_type_ == ProtocolType::UNKNOWN) {
            protocol_type_ = detectProtocol(recv_buffer_);
            if (protocol_type_ == ProtocolType::UNKNOWN) {
                std::cerr << "Unable to detect protocol\n";
                continue;
            }
            std::cout << "Protocol detected: " 
                     << (protocol_type_ == ProtocolType::RESP ? "RESP" : "Binary") << "\n";
        }

        // Parse messages based on protocol
        if (protocol_type_ == ProtocolType::RESP) {
            while (!recv_buffer_.empty()) {
                auto result = RespParser::parse(recv_buffer_);
                if (!result) break;

                auto [value, bytes_consumed] = *result;
                recv_buffer_.erase(0, bytes_consumed);
                handleCommand(value);
            }
        } else if (protocol_type_ == ProtocolType::BINARY) {
            while (!recv_buffer_.empty()) {
                auto result = BinaryProtocol::parse(recv_buffer_);
                if (!result) break;

                auto [frame, bytes_consumed] = *result;
                recv_buffer_.erase(0, bytes_consumed);
                handleBinaryFrame(frame);
            }
        }
    }

    std::cout << "Client disconnected: " << sock_fd_;
    LOG_INFO("Client disconnected: fd={}{}", sock_fd_, 
             client_id_.empty() ? "" : " client_id=" + client_id_);
    if (!client_id_.empty()) {
        std::cout << " (client: " << client_id_ << ")";
        broker_->unregisterClient(client_id_);
    }
    std::cout << "\n";
    
    broker_->removeSession(this);
    close_socket(sock_fd_);
}

void Session::handleBinaryFrame(const BinaryFrame& frame) {
    switch (frame.command) {
        case BinaryCommand::CMD_SUBSCRIBE: {
            // Topic format: "client_id\0topic" (client_id is optional, backward compatible)
            std::string topic = frame.topic;
            size_t sep = topic.find('\0');
            
            if (sep != std::string::npos) {
                // New format with client ID
                client_id_ = topic.substr(0, sep);
                topic = topic.substr(sep + 1);
                
                // Register client with broker
                broker_->registerClient(client_id_, this);
                
                // Use smart replay for this client
                broker_->subscribe(this, topic);
                broker_->replayMessagesForClient(this, topic, client_id_);
            } else {
                // Legacy format without client ID (replay all)
                broker_->subscribe(this, topic);
            }
            
            // Send ACK
            sendBinary(BinaryFrame::ack(frame.sequence));
            std::cout << "Binary SUBSCRIBE: " << topic;
            if (!client_id_.empty()) {
                std::cout << " (client: " << client_id_ << ")";
            }
            std::cout << "\n";
            break;
        }

        case BinaryCommand::CMD_UNSUBSCRIBE: {
            broker_->unsubscribe(this, frame.topic);
            sendBinary(BinaryFrame::ack(frame.sequence));
            break;
        }

        case BinaryCommand::CMD_PUBLISH: {
            // Check if topic requires signature (secure/ prefix)
            if (frame.topic.starts_with("secure/")) {
                sendBinary(BinaryFrame::error("Topic '" + frame.topic + "' requires signed publish (CMD_SIGNED_PUBLISH)"));
                std::cout << "REJECTED unsigned publish to secure topic: " << frame.topic << "\n";
                LOG_WARN("Rejected unsigned publish to secure topic '{}' from fd={}", frame.topic, sock_fd_);
                break;
            }

            // Create MESSAGE frame for subscribers
            BinaryFrame msg = BinaryFrame::message(frame.topic, frame.payload, ++sequence_);
            std::string serialized = BinaryProtocol::serialize(msg);
            broker_->publish(frame.topic, serialized);
            
            // Send ACK to publisher
            sendBinary(BinaryFrame::ack(frame.sequence));
            std::cout << "Binary PUBLISH: " << frame.topic << " (" << frame.payload.size() << " bytes)\n";
            break;
        }

        case BinaryCommand::CMD_PING: {
            sendBinary(BinaryFrame::pong());
            break;
        }

        case BinaryCommand::CMD_ACK: {
            // Handle acknowledgment from client
            if (!client_id_.empty()) {
                broker_->handleAck(client_id_, frame.sequence);
            }
            break;
        }

        case BinaryCommand::CMD_SIGNED_PUBLISH: {
            // Verify signature before publishing
            auto& keystore = crypto::get_global_keystore();
            
            // Build message to verify: topic + payload
            std::string message_to_verify = frame.topic + frame.payload;
            
            // Verify using stored public key
            bool valid = keystore.verify_with_key(
                frame.key_id,
                std::span<const uint8_t>(
                    reinterpret_cast<const uint8_t*>(message_to_verify.data()),
                    message_to_verify.size()
                ),
                frame.signature
            );
            
            if (!valid) {
                sendBinary(BinaryFrame::error("Invalid signature or unknown key"));
                std::cout << "REJECTED signed publish: invalid signature (key_id=" 
                          << frame.key_id << ")\n";
                LOG_WARN("Rejected signed publish: invalid signature key_id={} topic='{}' fd={}",
                         frame.key_id, frame.topic, sock_fd_);
                break;
            }
            
            // Check topic-scope authorization
            auto key_info = keystore.get_key_info(frame.key_id);
            if (key_info && !key_info->allowed_topics.empty()) {
                // allowed_topics can be a pattern like "sensors/*" or exact like "secure/data"
                const std::string& allowed = key_info->allowed_topics;
                bool topic_ok = false;
                
                if (allowed == "*" || allowed == "#") {
                    topic_ok = true;  // Wildcard: all topics
                } else if (allowed.size() >= 2 && allowed.back() == '*') {
                    // Prefix match: "sensors/*" matches "sensors/temp", "sensors/hum", etc.
                    std::string prefix = allowed.substr(0, allowed.size() - 1);
                    topic_ok = frame.topic.starts_with(prefix);
                } else {
                    topic_ok = (frame.topic == allowed);  // Exact match
                }
                
                if (!topic_ok) {
                    sendBinary(BinaryFrame::error("Key not authorized for topic '" + frame.topic + "'"));
                    std::cout << "REJECTED signed publish: key_id=" << frame.key_id
                              << " not authorized for topic '" << frame.topic << "'"
                              << " (allowed: '" << allowed << "')\n";
                    LOG_WARN("Rejected signed publish: key_id={} not authorized for topic '{}' (allowed: '{}')",
                             frame.key_id, frame.topic, allowed);
                    break;
                }
            }
            
            // Signature valid + topic authorized - create SIGNED_MESSAGE for subscribers
            BinaryFrame msg = BinaryFrame::signed_message(
                frame.topic, frame.payload, frame.signature, frame.key_id, ++sequence_
            );
            std::string serialized = BinaryProtocol::serialize(msg);
            broker_->publish(frame.topic, serialized);
            
            // Send ACK to publisher
            sendBinary(BinaryFrame::ack(frame.sequence));
            std::cout << "VERIFIED signed publish: " << frame.topic 
                      << " (key_id=" << frame.key_id << ")\n";
            LOG_INFO("Verified signed publish: topic='{}' key_id={} fd={}", 
                     frame.topic, frame.key_id, sock_fd_);
            break;
        }

        case BinaryCommand::CMD_SIGNED_MESSAGE:
            // This is broker->client only, ignore if received
            break;

        default:
            sendBinary(BinaryFrame::error("Unknown command"));
            break;
    }
}

void Session::sendBinary(const BinaryFrame& frame) {
    std::string data = BinaryProtocol::serialize(frame);
    send(data);
}

void Session::handleCommand(const RespValue& command) {
    if (!command.isArray()) {
        sendResp(RespValue::error("ERR Protocol error: expected array"));
        return;
    }

    const auto& arr = command.asArray();
    if (arr.empty()) {
        sendResp(RespValue::error("ERR empty command"));
        return;
    }

    std::string cmd = arr[0].asString();
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);

    if (cmd == "SUBSCRIBE") {
        // SUBSCRIBE topic1 [topic2 ...]
        for (size_t i = 1; i < arr.size(); ++i) {
            std::string topic = arr[i].asString();
            broker_->subscribe(this, topic);
            
            // Send subscription confirmation
            RespArray resp;
            resp.push_back(RespValue::bulkString("subscribe"));
            resp.push_back(RespValue::bulkString(topic));
            resp.push_back(RespValue::integer(static_cast<int64_t>(i)));
            sendResp(RespValue::array(resp));
        }
    }
    else if (cmd == "UNSUBSCRIBE") {
        // UNSUBSCRIBE topic1 [topic2 ...]
        for (size_t i = 1; i < arr.size(); ++i) {
            std::string topic = arr[i].asString();
            broker_->unsubscribe(this, topic);
            
            RespArray resp;
            resp.push_back(RespValue::bulkString("unsubscribe"));
            resp.push_back(RespValue::bulkString(topic));
            resp.push_back(RespValue::integer(0));
            sendResp(RespValue::array(resp));
        }
    }
    else if (cmd == "PUBLISH") {
        // PUBLISH topic message
        if (arr.size() != 3) {
            sendResp(RespValue::error("ERR wrong number of arguments for PUBLISH"));
            return;
        }
        
        std::string topic = arr[1].asString();
        std::string payload = arr[2].asString();
        
        // Create message array: ["message", topic, payload]
        RespArray msg;
        msg.push_back(RespValue::bulkString("message"));
        msg.push_back(RespValue::bulkString(topic));
        msg.push_back(RespValue::bulkString(payload));
        std::string serialized = RespParser::serialize(RespValue::array(msg));
        
        broker_->publish(topic, serialized);
        
        // Reply with number of subscribers (simplified: always return 1)
        sendResp(RespValue::integer(1));
    }
    else if (cmd == "PING") {
        // PING [message]
        if (arr.size() > 1) {
            sendResp(RespValue::bulkString(arr[1].asString()));
        } else {
            sendResp(RespValue::simpleString("PONG"));
        }
    }
    else if (cmd == "ACK") {
        // ACK sequence_id
        if (arr.size() != 2) {
            sendResp(RespValue::error("ERR wrong number of arguments for ACK"));
            return;
        }
        
        uint64_t seq = static_cast<uint64_t>(arr[1].asInteger());
        if (!client_id_.empty()) {
            broker_->handleAck(client_id_, seq);
        }
        sendResp(RespValue::simpleString("OK"));
    }
    else {
        sendResp(RespValue::error("ERR unknown command '" + cmd + "'"));
    }
}

void Session::sendResp(const RespValue& value) {
    std::string data = RespParser::serialize(value);
    send(data);
}

void Session::send(const std::string& data) {
    if (sock_fd_ == -1) return;
    int sent = ::send(sock_fd_, data.c_str(), static_cast<int>(data.size()), 0);
    if (sent == -1) {
        std::cerr << "Send failed\n";
    }
}

} // namespace metricmq
