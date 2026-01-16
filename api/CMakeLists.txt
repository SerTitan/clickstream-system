cmake_minimum_required(VERSION 3.16)
project(clickstream_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(api
    src/main.cpp
    src/http_server.cpp
    src/http_request.cpp
    src/http_response.cpp
    src/json.cpp
    src/metrics.cpp
    src/event_handler.cpp
    src/clickhouse_client.cpp
)

target_link_libraries(api pthread)