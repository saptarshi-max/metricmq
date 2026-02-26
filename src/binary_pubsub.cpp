// Binary Protocol Pub/Sub Implementation
#include "metricmq/binary_pubsub.hpp"
#include "binary_protocol.hpp"
#include "metricmq/crypto.hpp"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close_socket closesocket
#else
#include <unistd.h>
#include <arpa/inet.h>
#define close_socket close
#endif

namespace metricmq {

// BinaryPublisher
BinaryPublisher::BinaryPublisher(const std::string& host, int port) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "BinaryPublisher: socket creation failed\n";
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

#ifdef _WIN32
    addr.sin_addr.s_addr = inet_addr(host.c_str());
#else
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#endif

    if (connect(sock_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "BinaryPublisher: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    } else {
        std::cout << "BinaryPublisher: CONNECTED (binary protocol)\n";
    }
}

BinaryPublisher::~BinaryPublisher() {
    if (sock_ != -1) {
        close_socket(sock_);
    }
}

void BinaryPublisher::send(const std::string& topic, const std::string& payload) {
    if (sock_ == -1) {
        std::cerr << "BinaryPublisher: not connected\n";
        return;
    }

    // Create PUBLISH frame
    BinaryFrame frame = BinaryFrame::publish(topic, payload, ++sequence_);
    std::string data = BinaryProtocol::serialize(frame);
    
    ::send(sock_, data.c_str(), static_cast<int>(data.size()), 0);
    
    // Read ACK
    char buffer[256];
    int n = recv(sock_, buffer, sizeof(buffer), 0);
    if (n > 0) {
        std::string ack_buffer(buffer, n);
        auto result = BinaryProtocol::parse(ack_buffer);
        if (result && result->first.command == BinaryCommand::CMD_ACK) {
            // ACK received successfully
        }
    }
}

void BinaryPublisher::setSigningKey(const std::array<uint8_t, 64>& secret_key, uint32_t key_id) {
    secret_key_ = secret_key;
    signing_key_id_ = key_id;
    signing_enabled_ = true;
}

void BinaryPublisher::sendSigned(const std::string& topic, const std::string& payload) {
    if (sock_ == -1) {
        std::cerr << "BinaryPublisher: not connected\n";
        return;
    }
    if (!signing_enabled_) {
        std::cerr << "BinaryPublisher: signing not configured (call setSigningKey first)\n";
        return;
    }

    // Sign: message = topic + payload (matches broker verification in session.cpp)
    std::string message_to_sign = topic + payload;
    auto signature = crypto::sign(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(message_to_sign.data()),
                                 message_to_sign.size()),
        secret_key_
    );

    // Build signed frame
    BinaryFrame frame = BinaryFrame::signed_publish(topic, payload, signature, signing_key_id_, ++sequence_);
    std::string data = BinaryProtocol::serialize(frame);
    ::send(sock_, data.c_str(), static_cast<int>(data.size()), 0);

    // Read ACK or ERROR
    char buffer[256];
    int n = recv(sock_, buffer, sizeof(buffer), 0);
    if (n > 0) {
        std::string ack_buffer(buffer, n);
        auto result = BinaryProtocol::parse(ack_buffer);
        if (result) {
            if (result->first.command == BinaryCommand::CMD_ERROR) {
                std::cerr << "BinaryPublisher: signed publish REJECTED: " << result->first.payload << "\n";
            }
        }
    }
}

// BinarySubscriber
BinarySubscriber::BinarySubscriber(const std::string& host, int port) : auto_ack_(true) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "BinarySubscriber: socket creation failed\n";
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

#ifdef _WIN32
    addr.sin_addr.s_addr = inet_addr(host.c_str());
#else
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#endif

    if (connect(sock_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "BinarySubscriber: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    } else {
        std::cout << "BinarySubscriber: CONNECTED (binary protocol)\n";
    }
}

BinarySubscriber::BinarySubscriber(const std::string& client_id, const std::string& host, int port)
    : client_id_(client_id), auto_ack_(true) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "BinarySubscriber: socket creation failed\n";
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

#ifdef _WIN32
    addr.sin_addr.s_addr = inet_addr(host.c_str());
#else
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#endif

    if (connect(sock_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "BinarySubscriber: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    } else {
        std::cout << "BinarySubscriber: CONNECTED (binary protocol, client: " << client_id << ")\n";
    }
}

BinarySubscriber::~BinarySubscriber() {
    if (sock_ != -1) {
        close_socket(sock_);
    }
}

void BinarySubscriber::setClientId(const std::string& client_id) {
    client_id_ = client_id;
}

void BinarySubscriber::sendAck(uint64_t sequence) {
    if (sock_ == -1) return;
    
    BinaryFrame ack_frame = BinaryFrame::ack(sequence);
    std::string ack_data = BinaryProtocol::serialize(ack_frame);
    ::send(sock_, ack_data.c_str(), static_cast<int>(ack_data.size()), 0);
}

void BinarySubscriber::subscribe(const std::string& topic,
                                 std::function<void(const std::string& topic, const std::string& payload)> callback,
                                 bool auto_ack) {
    if (sock_ == -1) {
        std::cerr << "BinarySubscriber: not connected\n";
        return;
    }

    auto_ack_ = auto_ack;

    // Send SUBSCRIBE frame with optional client ID
    std::string subscribe_topic = topic;
    if (!client_id_.empty()) {
        // Embed client ID: "client_id\0topic"
        subscribe_topic = client_id_ + std::string(1, '\0') + topic;
    }
    
    BinaryFrame frame = BinaryFrame::subscribe(subscribe_topic, ++sequence_);
    std::string data = BinaryProtocol::serialize(frame);
    ::send(sock_, data.c_str(), static_cast<int>(data.size()), 0);

    // Receive loop
    std::string recv_buffer;
    char buffer[4096];
    
    while (true) {
        int n = recv(sock_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::cerr << "BinarySubscriber: connection closed\n";
            break;
        }

        recv_buffer.append(buffer, n);

        // Parse binary frames
        while (!recv_buffer.empty()) {
            auto result = BinaryProtocol::parse(recv_buffer);
            if (!result) break;  // Incomplete frame

            auto [frame, bytes_consumed] = *result;
            recv_buffer.erase(0, bytes_consumed);

            // Handle MESSAGE and SIGNED_MESSAGE frames
            if (frame.command == BinaryCommand::CMD_MESSAGE ||
                frame.command == BinaryCommand::CMD_SIGNED_MESSAGE) {
                // Call user callback (basic callback ignores signature metadata)
                callback(frame.topic, frame.payload);
                
                // Send ACK if enabled
                if (auto_ack_) {
                    sendAck(frame.sequence);
                }
            } else if (frame.command == BinaryCommand::CMD_ACK) {
                // Subscription confirmed
                std::cout << "BinarySubscriber: Subscribed to '" << topic << "'";
                if (!client_id_.empty()) {
                    std::cout << " (client: " << client_id_ << ")";
                }
                std::cout << "\n";
            }
        }
    }
}

void BinarySubscriber::subscribeSigned(const std::string& topic,
                                       std::function<void(const SignedMessageInfo& msg)> callback,
                                       bool auto_ack) {
    if (sock_ == -1) {
        std::cerr << "BinarySubscriber: not connected\n";
        return;
    }

    auto_ack_ = auto_ack;

    // Send SUBSCRIBE frame with optional client ID
    std::string subscribe_topic = topic;
    if (!client_id_.empty()) {
        subscribe_topic = client_id_ + std::string(1, '\0') + topic;
    }

    BinaryFrame frame = BinaryFrame::subscribe(subscribe_topic, ++sequence_);
    std::string data = BinaryProtocol::serialize(frame);
    ::send(sock_, data.c_str(), static_cast<int>(data.size()), 0);

    // Receive loop
    std::string recv_buffer;
    char buffer[4096];

    while (true) {
        int n = recv(sock_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::cerr << "BinarySubscriber: connection closed\n";
            break;
        }

        recv_buffer.append(buffer, n);

        while (!recv_buffer.empty()) {
            auto result = BinaryProtocol::parse(recv_buffer);
            if (!result) break;

            auto [frame, bytes_consumed] = *result;
            recv_buffer.erase(0, bytes_consumed);

            if (frame.command == BinaryCommand::CMD_MESSAGE ||
                frame.command == BinaryCommand::CMD_SIGNED_MESSAGE) {
                
                SignedMessageInfo info;
                info.topic = frame.topic;
                info.payload = frame.payload;
                info.sequence = frame.sequence;
                info.is_signed = frame.is_signed;
                info.key_id = frame.key_id;
                if (frame.is_signed) {
                    info.signature = frame.signature;
                }

                callback(info);

                if (auto_ack_) {
                    sendAck(frame.sequence);
                }
            } else if (frame.command == BinaryCommand::CMD_ACK) {
                std::cout << "BinarySubscriber: Subscribed to '" << topic << "'\n";
            }
        }
    }
}

void BinarySubscriber::run() {
    if (sock_ == -1) {
        std::cerr << "BinarySubscriber: not connected\n";
        return;
    }

    std::cout << "BinarySubscriber: running (debug mode)...\n";
    
    std::string recv_buffer;
    char buffer[4096];
    
    while (true) {
        int n = recv(sock_, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        
        recv_buffer.append(buffer, n);
        
        while (!recv_buffer.empty()) {
            auto result = BinaryProtocol::parse(recv_buffer);
            if (!result) break;
            
            auto [frame, bytes_consumed] = *result;
            recv_buffer.erase(0, bytes_consumed);
            
            std::cout << "Frame received: cmd=" << static_cast<int>(frame.command)
                     << " seq=" << frame.sequence
                     << " topic=" << frame.topic
                     << " payload_size=" << frame.payload.size() << "\n";
        }
    }
}

} // namespace metricmq
