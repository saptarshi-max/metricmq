/**
 * @file metrics_server.hpp
 * @brief HTTP server that exposes a Prometheus `/metrics` endpoint.
 *
 * Backed by the Poco::Net HTTP framework. Runs on its own thread pool (managed
 * by Poco). `MetricsServer::start()` is non-blocking.
 *
 * @par Endpoint
 * `GET http://localhost:9091/metrics` → Prometheus text format (0.0.4)
 *
 * @par Usage
 * @code
 * metricmq::MetricsServer srv(9091);
 * srv.start();   // non-blocking
 * // ... broker runs ...
 * srv.stop();
 * @endcode
 */
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

/**
 * @brief Poco HTTP request handler for `GET /metrics`.
 *
 * Calls `Metrics::instance().exportPrometheus()` and writes the result with
 * `Content-Type: text/plain; version=0.0.4`.
 * All other paths return HTTP 404.
 */
class MetricsHandler : public Poco::Net::HTTPRequestHandler {
public:
    /**
     * @brief Serve one HTTP request.
     * @param request  Incoming HTTP request (method, URI, headers).
     * @param response Outgoing HTTP response to populate.
     */
    void handleRequest(Poco::Net::HTTPServerRequest& request,
                       Poco::Net::HTTPServerResponse& response) override;
};

/**
 * @brief Poco factory that creates a `MetricsHandler` for every request.
 * @internal Used internally by Poco::Net::HTTPServer. Not part of the public API.
 */
class MetricsHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
public:
    Poco::Net::HTTPRequestHandler* createRequestHandler(
        const Poco::Net::HTTPServerRequest& request) override;
};

/**
 * @brief Lifecycle controller for the HTTP metrics server.
 *
 * Start once after broker initialization; stop during graceful shutdown.
 */
class MetricsServer {
public:
    /**
     * @brief Construct the server (does not start listening yet).
     * @param port TCP port to listen on (default: 9091).
     */
    explicit MetricsServer(int port = 9091);

    /** @brief Stops the server if still running. */
    ~MetricsServer();

    /**
     * @brief Start listening for HTTP connections.
     *
     * Non-blocking. Poco manages its own thread pool for request handling.
     */
    void start();

    /**
     * @brief Gracefully stop the server and release the port.
     *
     * Waits for in-flight requests to complete before returning.
     */
    void stop();

    /** @brief Returns `true` if `start()` has been called and `stop()` has not. */
    bool isRunning() const;

private:
    int port_;
    std::unique_ptr<Poco::Net::HTTPServer> server_;
    std::atomic<bool> running_{false};
};

} // namespace metricmq
