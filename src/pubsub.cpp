// src/pubsub.cpp - RESP-based implementation
#include "metricmq/pubsub.hpp"
#include "resp_parser.hpp"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close_socket closesocket
#define sleep_ms(ms) Sleep(ms)
#else
#include <unistd.h>
#include <arpa/inet.h>
#define close_socket close
#define sleep_ms(ms) usleep(ms * 1000)
#endif

namespace metricmq {

// Windows Winsock initialization (one-time, thread-safe)
#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA wsa;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (result != 0) {
            std::cerr << "WSAStartup failed: " << result << "\n";
        }
    }
    ~WinsockInit() {
        WSACleanup();
    }
};
static WinsockInit winsock_init;
#endif

// Publisher
Publisher::Publisher(const std::string& host, int port) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "Publisher: socket creation failed\n";
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
        std::cerr << "Publisher: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    }
}

Publisher::~Publisher() {
    if (sock_ != -1) {
        close_socket(sock_);
    }
}

void Publisher::send(const std::string& topic, const std::string& payload) {
    if (sock_ == -1) {
        std::cerr << "Publisher: not connected\n";
        return;
    }

    // Create RESP array: ["PUBLISH", topic, payload]
    RespValue cmd = RespParser::makeArray({"PUBLISH", topic, payload});
    std::string msg = RespParser::serialize(cmd);
    
    ::send(sock_, msg.c_str(), static_cast<int>(msg.size()), 0);
    
    // Read response (integer: number of subscribers)
    char buffer[1024];
    int n = recv(sock_, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        // Optional: parse response to verify delivery
    }
}

// Subscriber
Subscriber::Subscriber(const std::string& host, int port) {
    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        std::cerr << "Subscriber: socket creation failed\n";
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
        std::cerr << "Subscriber: connect failed\n";
        close_socket(sock_);
        sock_ = -1;
    } else {
        std::cout << "Subscriber CONNECTED successfully to " << host << ":" << port << "\n";
    }
}

Subscriber::~Subscriber() {
    if (sock_ != -1) {
        close_socket(sock_);
    }
}

void Subscriber::subscribe(const std::string& topic,
                           std::function<void(const std::string& topic, const std::string& payload)> callback) {
    if (this->sock_ == -1) {
        std::cerr << "Subscriber: not connected\n";
        return;
    }

    // Send SUBSCRIBE command via RESP
    RespValue cmd = RespParser::makeArray({"SUBSCRIBE", topic});
    std::string sub_msg = RespParser::serialize(cmd);
    ::send(this->sock_, sub_msg.c_str(), static_cast<int>(sub_msg.size()), 0);

    // Receive loop - parse RESP messages
    std::string recv_buffer;
    char buffer[4096];
    
    while (true) {
        int n = recv(this->sock_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::cerr << "Subscriber: connection closed\n";
            break;
        }

        recv_buffer.append(buffer, n);

        // Parse RESP messages from buffer
        while (!recv_buffer.empty()) {
            auto result = RespParser::parse(recv_buffer);
            if (!result) {
                // Incomplete message, wait for more data
                break;
            }

            auto [value, bytes_consumed] = *result;
            recv_buffer.erase(0, bytes_consumed);

            // Handle subscription confirmation and messages
            if (value.isArray()) {
                const auto& arr = value.asArray();
                if (arr.size() >= 3) {
                    std::string msg_type = arr[0].asString();
                    
                    if (msg_type == "message") {
                        // Message format: ["message", topic, payload]
                        std::string received_topic = arr[1].asString();
                        std::string payload = arr[2].asString();
                        callback(received_topic, payload);
                    }
                    // Ignore "subscribe" confirmations
                }
            }
        }
    }
}

void Subscriber::run() {
    if (this->sock_ == -1) {
        std::cerr << "Subscriber: not connected\n";
        return;
    }

    std::cout << "Subscriber running (blocking receive loop)...\n";
    
    std::string recv_buffer;
    char buffer[4096];
    
    while (true) {
        int n = recv(this->sock_, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            std::cerr << "Subscriber: connection closed\n";
            break;
        }
        
        recv_buffer.append(buffer, n);
        
        while (!recv_buffer.empty()) {
            auto result = RespParser::parse(recv_buffer);
            if (!result) break;
            
            auto [value, bytes_consumed] = *result;
            recv_buffer.erase(0, bytes_consumed);
            
            std::cout << "Raw RESP received: " << RespParser::serialize(value);
        }
    }
}

void Subscriber::sendAck(uint64_t sequence) {
    if (sock_ == -1) return;
    
    // Send ACK command: ["ACK", sequence]
    RespValue cmd = RespParser::makeArray({"ACK", std::to_string(sequence)});
    std::string ack_msg = RespParser::serialize(cmd);
    ::send(sock_, ack_msg.c_str(), static_cast<int>(ack_msg.size()), 0);
}

} // namespace metricmq
