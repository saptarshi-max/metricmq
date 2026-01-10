#pragma once
#include <string>

namespace metricmq {

class Broker;
class RespValue;
struct BinaryFrame;

enum class ProtocolType {
    UNKNOWN,
    RESP,
    BINARY
};

class Session {
public:
    Session(int sock_fd, Broker* broker);
    void run();  // blocking receive loop
    void send(const std::string& data);
    void sendWithSequence(uint64_t sequence, const std::string& data);
    
    const std::string& getClientId() const { return client_id_; }
    void setClientId(const std::string& client_id) { client_id_ = client_id; }

private:
    void handleCommand(const RespValue& command);
    void handleBinaryFrame(const BinaryFrame& frame);
    void sendResp(const RespValue& value);
    void sendBinary(const BinaryFrame& frame);
    ProtocolType detectProtocol(const std::string& buffer);
    
    int sock_fd_;
    Broker* broker_;
    std::string recv_buffer_;  // Buffer for incomplete messages
    ProtocolType protocol_type_;
    uint64_t sequence_;
    std::string client_id_;  // Persistent client identifier for exactly-once
};

} // namespace metricmq