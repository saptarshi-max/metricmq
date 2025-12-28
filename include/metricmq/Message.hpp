#pragma once
#include <string>

namespace metricmq {

struct Message {
    std::string topic;
    std::string payload;
};

} // namespace metricmq