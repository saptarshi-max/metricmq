// RESP Protocol Parser Implementation
#include "resp_parser.hpp"
#include <sstream>
#include <stdexcept>

namespace metricmq {

static std::optional<size_t> findCRLF(const std::string& buffer, size_t start = 0) {
    size_t pos = buffer.find("\r\n", start);
    if (pos == std::string::npos) return std::nullopt;
    return pos;
}

std::optional<std::pair<RespValue, size_t>> RespParser::parse(const std::string& buffer) {
    if (buffer.empty()) return std::nullopt;

    char type_char = buffer[0];
    size_t pos = 1;

    switch (type_char) {
        case '+': { // Simple String
            auto crlf = findCRLF(buffer, pos);
            if (!crlf) return std::nullopt;
            
            std::string str = buffer.substr(pos, *crlf - pos);
            return std::make_pair(RespValue::simpleString(str), *crlf + 2);
        }

        case '-': { // Error
            auto crlf = findCRLF(buffer, pos);
            if (!crlf) return std::nullopt;
            
            std::string err = buffer.substr(pos, *crlf - pos);
            return std::make_pair(RespValue::error(err), *crlf + 2);
        }

        case ':': { // Integer
            auto crlf = findCRLF(buffer, pos);
            if (!crlf) return std::nullopt;
            
            std::string num_str = buffer.substr(pos, *crlf - pos);
            int64_t num = std::stoll(num_str);
            return std::make_pair(RespValue::integer(num), *crlf + 2);
        }

        case '$': { // Bulk String
            auto crlf = findCRLF(buffer, pos);
            if (!crlf) return std::nullopt;
            
            std::string len_str = buffer.substr(pos, *crlf - pos);
            int64_t len = std::stoll(len_str);
            
            if (len == -1) {
                // Null bulk string
                return std::make_pair(RespValue::null(), *crlf + 2);
            }
            
            size_t data_start = *crlf + 2;
            if (buffer.size() < data_start + len + 2) {
                return std::nullopt; // incomplete
            }
            
            std::string str = buffer.substr(data_start, len);
            return std::make_pair(RespValue::bulkString(str), data_start + len + 2);
        }

        case '*': { // Array
            auto crlf = findCRLF(buffer, pos);
            if (!crlf) return std::nullopt;
            
            std::string count_str = buffer.substr(pos, *crlf - pos);
            int64_t count = std::stoll(count_str);
            
            if (count == -1) {
                // Null array
                return std::make_pair(RespValue::null(), *crlf + 2);
            }
            
            RespArray arr;
            size_t offset = *crlf + 2;
            
            for (int64_t i = 0; i < count; ++i) {
                auto elem = parse(buffer.substr(offset));
                if (!elem) return std::nullopt; // incomplete
                
                arr.push_back(elem->first);
                offset += elem->second;
            }
            
            return std::make_pair(RespValue::array(arr), offset);
        }

        default:
            throw std::runtime_error("Invalid RESP type: " + std::string(1, type_char));
    }
}

std::string RespParser::serialize(const RespValue& value) {
    std::ostringstream oss;

    switch (value.type) {
        case RespType::SimpleString:
            oss << '+' << value.asString() << "\r\n";
            break;

        case RespType::Error:
            oss << '-' << value.asString() << "\r\n";
            break;

        case RespType::Integer:
            oss << ':' << value.asInteger() << "\r\n";
            break;

        case RespType::BulkString:
            oss << '$' << value.asString().size() << "\r\n"
                << value.asString() << "\r\n";
            break;

        case RespType::Array: {
            const auto& arr = value.asArray();
            oss << '*' << arr.size() << "\r\n";
            for (const auto& elem : arr) {
                oss << serialize(elem);
            }
            break;
        }

        case RespType::Null:
            oss << "$-1\r\n";
            break;
    }

    return oss.str();
}

RespValue RespParser::makeArray(const std::vector<std::string>& strings) {
    RespArray arr;
    for (const auto& str : strings) {
        arr.push_back(RespValue::bulkString(str));
    }
    return RespValue::array(arr);
}

} // namespace metricmq
