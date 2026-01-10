cmake_minimum_required(VERSION 3.30)
project(MetricMQ CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan toolchain (generated in build/)
include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

# Force C++17 minimum after toolchain inclusion
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Override Conan toolchain C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Poco REQUIRED COMPONENTS Net Util JSON)
find_package(Boost REQUIRED)  # No COMPONENTS needed anymore
find_package(lmdb REQUIRED)
find_package(spdlog REQUIRED)
find_package(benchmark REQUIRED)

# Core library
add_library(metricmq_lib STATIC
    src/broker.cpp
    src/session.cpp
    src/resp_parser.cpp
    src/binary_protocol.cpp
    src/pubsub.cpp
    src/binary_pubsub.cpp
    src/queue.cpp
    src/storage/LmdbStorage.cpp
    src/logger.cpp
    src/metrics.cpp
    src/metrics_server.cpp
)

target_include_directories(metricmq_lib PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    "C:/Users/Sapta/.conan2/p/lmdbf2a2ec8d6cae2/p/include"
    "C:/Users/Sapta/.conan2/p/spdloa333e6e11c7ff/p/include"
    "C:/Users/Sapta/.conan2/p/fmtefbe862283345/p/include"
)
target_link_libraries(metricmq_lib PUBLIC
    Poco::Net Poco::Util Poco::JSON
    boost::boost  # Includes asio, beast, system, etc.
    lmdb::lmdb
    spdlog::spdlog
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(metricmq_lib PUBLIC ws2_32)
endif()

# Broker executable
add_executable(metricmq-broker src/main.cpp)
target_link_libraries(metricmq-broker PRIVATE metricmq_lib)

# Examples (link to library)
add_executable(simple_pub_sub examples/pubsub_raw_tcp.cpp)
target_link_libraries(simple_pub_sub PRIVATE metricmq_lib)

add_executable(pub_only examples/pub_only.cpp)
target_link_libraries(pub_only PRIVATE metricmq_lib)

add_executable(sub_only examples/sub_only.cpp)
target_link_libraries(sub_only PRIVATE metricmq_lib)

add_executable(pubsub_redis examples/pubsub_redis.cpp)
target_link_libraries(pubsub_redis PRIVATE metricmq_lib)

add_executable(throughput benchmark/throughput.cpp)
target_link_libraries(throughput PRIVATE metricmq_lib)

add_executable(protocol_benchmark benchmark/protocol_benchmark.cpp)
target_link_libraries(protocol_benchmark PRIVATE metricmq_lib)

# Binary protocol examples
add_executable(binary_pub_only examples/binary_pub_only.cpp)
target_link_libraries(binary_pub_only PRIVATE metricmq_lib)

add_executable(binary_sub_only examples/binary_sub_only.cpp)
target_link_libraries(binary_sub_only PRIVATE metricmq_lib)

# Queue mode examples
add_executable(push_only examples/push_only.cpp)
target_link_libraries(push_only PRIVATE metricmq_lib)

add_executable(pull_only examples/pull_only.cpp)
target_link_libraries(pull_only PRIVATE metricmq_lib)

add_executable(persistence_test examples/persistence_test.cpp)
target_link_libraries(persistence_test PRIVATE metricmq_lib)

add_executable(exactly_once_test examples/exactly_once_test.cpp)
target_link_libraries(exactly_once_test PRIVATE metricmq_lib)

# Google Benchmark suite
add_executable(latency_benchmark benchmark/latency_benchmark.cpp)
target_link_libraries(latency_benchmark PRIVATE metricmq_lib benchmark::benchmark_main)

add_executable(throughput_benchmark benchmark/throughput_benchmark.cpp)
target_link_libraries(throughput_benchmark PRIVATE metricmq_lib benchmark::benchmark_main)

add_executable(protocol_comparison_benchmark benchmark/protocol_benchmark.cpp)
target_link_libraries(protocol_comparison_benchmark PRIVATE metricmq_lib benchmark::benchmark_main)

add_executable(persistence_benchmark benchmark/persistence_benchmark.cpp)
target_link_libraries(persistence_benchmark PRIVATE metricmq_lib benchmark::benchmark_main)
