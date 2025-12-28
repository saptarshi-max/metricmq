#pragma once

namespace metricmq {

struct Config {
    int port = 6379;
    bool persistence = true;
};

} // namespace metricmq