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
#include <iostream>
#include <string>      // for std::string
#include <memory>      // for std::shared_ptr
#include <cstring>     // for memset

namespace metricmq {

Session::Session(int sock_fd, Broker* broker)
    : sock_fd_(sock_fd), broker_(broker) {
    std::cout << "New client connected: " << sock_fd << "\n";
}

void Session::run() {
    char buffer[4096];
    while (true) {
        int n = recv(sock_fd_, buffer, sizeof(buffer) - 1, 0);
        if (n < 0) {
            std::cerr << "Recv error: " << n << "\n";
            break;
        }
        if (n == 0) {
            break;  // connection closed gracefully
        }

        buffer[n] = '\0';
        std::string message(buffer);

        std::cout << "Received from client: " << message << "\n";

        // Broadcast to all other clients (simple pub/sub relay)
        for (auto& s : broker_->sessions_) {
            if (s.get() != this) {  // don't send back to sender
                s->send(message);
                std::cout << "Relayed to client: " << s->sock_fd_ << "\n";  // debug print
            }
        }
    }

    std::cout << "Client disconnected: " << sock_fd_ << "\n";
    close_socket(sock_fd_);
}

void Session::send(const std::string& data) {
    if (sock_fd_ == -1) return;
    int sent = ::send(sock_fd_, data.c_str(), static_cast<int>(data.size()), 0);
    if (sent == -1) {
        std::cerr << "Send failed\n";
    }
}

} // namespace metricmq