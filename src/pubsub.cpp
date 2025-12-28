// src/pubsub.cpp
#include "metricmq/pubsub.hpp"
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

    std::string msg = topic + "|" + payload + "\n";
    ::send(sock_, msg.c_str(), static_cast<int>(msg.size()), 0);
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

    char buffer[4096];
    while (true) {
        int n = recv(this->sock_, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            std::cerr << "Subscriber: connection closed\n";
            break;
        }

        buffer[n] = '\0';
        std::string line(buffer);

        size_t pos = line.find('|');
        if (pos != std::string::npos) {
            std::string received_topic = line.substr(0, pos);
            std::string payload = line.substr(pos + 1);
            if (!payload.empty() && payload.back() == '\n') {
                payload.pop_back();
            }

            if (topic == "#" || received_topic == topic) {
                callback(received_topic, payload);
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
    char buffer[4096];
    while (true) {
        int n = recv(this->sock_, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            std::cerr << "Subscriber: connection closed\n";
            break;
        }
        buffer[n] = '\0';
        std::cout << "Raw received: " << buffer << "\n";
    }
}

} // namespace metricmq