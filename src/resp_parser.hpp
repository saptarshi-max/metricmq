// RESP (REdis Serialization Protocol) Parser & Serializer
#pragma once
#include <string>
#include <vector>
#include <variant>
#include <optional>

namespace metricmq {

// RESP Value types
enum class RespType {
    SimpleString,  // +OK\r\n
    Error,         // -ERR message\r\n
    Integer,       // :1000\r\n
    BulkString,    // $6\r\nfoobar\r\n
    Array,         // *2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
    Null           // $-1\r\n
};

class RespValue;

using RespArray = std::vector<RespValue>;
using RespVariant = std::variant<
    std::string,      // SimpleString, Error, BulkString
    int64_t,          // Integer
    RespArray,        // Array
    std::monostate    // Null
>;

class RespValue {
public:
    RespType type;
    RespVariant value;

    // Constructors
    RespValue() : type(RespType::Null), value(std::monostate{}) {}
    
    static RespValue simpleString(const std::string& str) {
        RespValue v;
        v.type = RespType::SimpleString;
        v.value = str;
        return v;
    }
    
    static RespValue error(const std::string& msg) {
        RespValue v;
        v.type = RespType::Error;
        v.value = msg;
        return v;
    }
    
    static RespValue integer(int64_t num) {
        RespValue v;
        v.type = RespType::Integer;
        v.value = num;
        return v;
    }
    
    static RespValue bulkString(const std::string& str) {
        RespValue v;
        v.type = RespType::BulkString;
        v.value = str;
        return v;
    }
    
    static RespValue array(const RespArray& arr) {
        RespValue v;
        v.type = RespType::Array;
        v.value = arr;
        return v;
    }
    
    static RespValue null() {
        return RespValue();
    }

    // Accessors
    bool isArray() const { return type == RespType::Array; }
    bool isString() const { return type == RespType::BulkString || type == RespType::SimpleString; }
    bool isError() const { return type == RespType::Error; }
    bool isInteger() const { return type == RespType::Integer; }
    bool isNull() const { return type == RespType::Null; }

    std::string asString() const {
        if (auto* str = std::get_if<std::string>(&value)) {
            return *str;
        }
        return "";
    }

    int64_t asInteger() const {
        if (auto* num = std::get_if<int64_t>(&value)) {
            return *num;
        }
        return 0;
    }

    const RespArray& asArray() const {
        if (auto* arr = std::get_if<RespArray>(&value)) {
            return *arr;
        }
        static RespArray empty;
        return empty;
    }
};

class RespParser {
public:
    // Parse RESP from buffer, returns parsed value and bytes consumed
    // Returns nullopt if incomplete message
    static std::optional<std::pair<RespValue, size_t>> parse(const std::string& buffer);

    // Serialize RespValue to RESP protocol string
    static std::string serialize(const RespValue& value);

    // Helper: create RESP array from strings (common for commands)
    static RespValue makeArray(const std::vector<std::string>& strings);
};

} // namespace metricmq
