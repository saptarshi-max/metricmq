cmake_minimum_required(VERSION 3.30)
project(MetricMQ CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan toolchain (generated in build/)
include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)

find_package(Poco REQUIRED COMPONENTS Net Util JSON)
find_package(Boost REQUIRED)  # No COMPONENTS needed anymore

# Core library
add_library(metricmq_lib STATIC
    src/broker.cpp
    src/session.cpp
    src/resp_parser.cpp
    src/pubsub.cpp
)

target_include_directories(metricmq_lib PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(metricmq_lib PUBLIC
    Poco::Net Poco::Util Poco::JSON
    Boost::boost  # Includes asio, beast, system, etc.
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