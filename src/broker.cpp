// Broker Core Logic

#if defined(_WIN32) || defined(WIN32)
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#define close closesocket
#define sleep_ms(ms) Sleep(ms)
#define SOCKLEN_T int
#else
#include <unistd.h>
#include <arpa/inet.h>
#define sleep_ms(ms) usleep(ms * 1000)
#define SOCKLEN_T socklen_t
#endif

#include "broker.hpp"
#include "session.hpp"
#include <iostream>
#include <cstring>
#include <thread>


namespace metricmq {

Broker::Broker(int port) : port_(port) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif
}

void Broker::run() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 10);

    std::cout << "Broker listening on port " << port_ << "\n";

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        auto session = std::make_shared<Session>(client, this);
        sessions_.push_back(session);
        std::thread([session] { session->run(); }).detach();
    }
}

} // namespace