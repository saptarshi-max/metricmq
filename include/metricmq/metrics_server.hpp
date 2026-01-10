// include/metricmq/metrics_server.hpp
#pragma once
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/HTTPRequestHandler.h>
#include <Poco/Net/HTTPRequestHandlerFactory.h>
#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/Net/HTTPServerResponse.h>
#include <memory>
#include <atomic>

namespace metricmq {

// HTTP handler for /metrics endpoint
class MetricsHandler : public Poco::Net::HTTPRequestHandler {
public:
    void handleRequest(Poco::Net::HTTPServerRequest& request,
                      Poco::Net::HTTPServerResponse& response) override;
};

// Factory for creating metrics handlers
class MetricsHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;
};

// Metrics HTTP server
class MetricsServer {
public:
    explicit MetricsServer(int port = 9091);
    ~MetricsServer();
    
    void start();
    void stop();
    bool isRunning() const;
    
private:
    int port_;
    std::unique_ptr<Poco::Net::HTTPServer> server_;
    std::atomic<bool> running_{false};
};

} // namespace metricmq
