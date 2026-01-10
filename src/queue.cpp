// Queue Mode Implementation (PUSH/PULL)
#include "metricmq/queue.hpp"
#include "binary_protocol.hpp"
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

// QueueProducer
QueueProducer::QueueProducer(const std::string& host, int port) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "QueueProducer: socket creation failed\n";
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
        std::cerr << "QueueProducer: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    } else {
        std::cout << "QueueProducer: CONNECTED (queue mode)\n";
    }
}

QueueProducer::~QueueProducer() {
    if (sock_ != -1) {
        close_socket(sock_);
    }
}

void QueueProducer::push(const std::string& queue_name, const std::string& payload) {
    if (sock_ == -1) {
        std::cerr << "QueueProducer: not connected\n";
        return;
    }

    // Create PUSH frame (reuse PUBLISH command with queue prefix)
    // Convention: queue_name starts with "q:" to distinguish from pub/sub
    BinaryFrame frame = BinaryFrame::publish("q:" + queue_name, payload, ++sequence_);
    std::string data = BinaryProtocol::serialize(frame);
    
    ::send(sock_, data.c_str(), static_cast<int>(data.size()), 0);
    
    // Read ACK
    char buffer[256];
    int n = recv(sock_, buffer, sizeof(buffer), 0);
    if (n > 0) {
        std::string ack_buffer(buffer, n);
        auto result = BinaryProtocol::parse(ack_buffer);
        if (result && result->first.command == BinaryCommand::CMD_ACK) {
            // ACK received
        }
    }
}

// QueueConsumer
QueueConsumer::QueueConsumer(const std::string& host, int port) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "QueueConsumer: socket creation failed\n";
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
        std::cerr << "QueueConsumer: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    } else {
        std::cout << "QueueConsumer: CONNECTED (queue mode)\n";
    }
}

QueueConsumer::~QueueConsumer() {
    if (sock_ != -1) {
        close_socket(sock_);
    }
}

void QueueConsumer::pull(const std::string& queue_name,
                         std::function<void(const std::string& payload)> callback) {
    if (sock_ == -1) {
        std::cerr << "QueueConsumer: not connected\n";
        return;
    }

    // Send SUBSCRIBE frame for queue (q: prefix)
    BinaryFrame frame = BinaryFrame::subscribe("q:" + queue_name, ++sequence_);
    std::string data = BinaryProtocol::serialize(frame);
    ::send(sock_, data.c_str(), static_cast<int>(data.size()), 0);

    // Receive loop
    std::string recv_buffer;
    char buffer[4096];
    
    while (true) {
        int n = recv(sock_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::cerr << "QueueConsumer: connection closed\n";
            break;
        }

        recv_buffer.append(buffer, n);

        while (!recv_buffer.empty()) {
            auto result = BinaryProtocol::parse(recv_buffer);
            if (!result) break;

            auto [frame, bytes_consumed] = *result;
            recv_buffer.erase(0, bytes_consumed);

            if (frame.command == BinaryCommand::CMD_MESSAGE) {
                // Extract queue name (remove "q:" prefix)
                std::string topic = frame.topic;
                if (topic.substr(0, 2) == "q:") {
                    callback(frame.payload);
                }
            } else if (frame.command == BinaryCommand::CMD_ACK) {
                std::cout << "QueueConsumer: Pulled from '" << queue_name << "'\n";
            }
        }
    }
}

void QueueConsumer::run() {
    if (sock_ == -1) {
        std::cerr << "QueueConsumer: not connected\n";
        return;
    }

    std::cout << "QueueConsumer: running (debug mode)...\n";
    
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
            
            std::cout << "Queue task: " << frame.topic << " = " << frame.payload << "\n";
        }
    }
}

} // namespace metricmq
