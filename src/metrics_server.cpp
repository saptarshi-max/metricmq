// src/metrics_server.cpp
#include "metricmq/metrics_server.hpp"
#include "metricmq/metrics.hpp"
#include "metricmq/logger.hpp"
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/HTTPServerParams.h>
#include <sstream>

namespace metricmq {

void MetricsHandler::handleRequest(Poco::Net::HTTPServerRequest& request,
                                   Poco::Net::HTTPServerResponse& response) {
    LOG_DEBUG("Metrics request from {}", request.clientAddress().toString());
    
    // Only support GET requests
    if (request.getMethod() != "GET") {
        response.setStatus(Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED);
        response.send();
        return;
    }
    
    // Export metrics in Prometheus format
    std::string metrics = Metrics::instance().exportPrometheus();
    
    // Set response headers
    response.setContentType("text/plain; version=0.0.4");
    response.setContentLength(metrics.length());
    response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
    
    // Send response
    std::ostream& out = response.send();
    out << metrics;
    out.flush();
}

Poco::Net::HTTPRequestHandler* MetricsHandlerFactory::createRequestHandler(
    const Poco::Net::HTTPServerRequest& request) {
    
    // Only handle /metrics endpoint
    if (request.getURI() == "/metrics" || request.getURI() == "/metrics/") {
        return new MetricsHandler();
    }
    
    // Return 404 for other paths
    return nullptr;
}

MetricsServer::MetricsServer(int port) : port_(port) {
}

MetricsServer::~MetricsServer() {
    stop();
}

void MetricsServer::start() {
    if (running_) {
        LOG_WARN("Metrics server already running");
        return;
    }
    
    try {
        LOG_INFO("Starting metrics server on port {}", port_);
        
        // Create server socket
        Poco::Net::ServerSocket socket(port_);
        
        // Configure HTTP server parameters
        auto params = new Poco::Net::HTTPServerParams();
        params->setMaxQueued(100);
        params->setMaxThreads(4);
        params->setKeepAlive(true);
        params->setTimeout(Poco::Timespan(60, 0));  // 60 seconds
        
        // Create and start HTTP server
        server_ = std::make_unique<Poco::Net::HTTPServer>(
            new MetricsHandlerFactory(),
            socket,
            params
        );
        
        server_->start();
        running_ = true;
        
        LOG_INFO("Metrics server started successfully on http://0.0.0.0:{}/metrics", port_);
        
    } catch (const Poco::Exception& ex) {
        LOG_ERROR("Failed to start metrics server: {}", ex.displayText());
        throw;
    }
}

void MetricsServer::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping metrics server");
    
    if (server_) {
        server_->stop();
        server_.reset();
    }
    
    running_ = false;
    LOG_INFO("Metrics server stopped");
}

bool MetricsServer::isRunning() const {
    return running_;
}

} // namespace metricmq
