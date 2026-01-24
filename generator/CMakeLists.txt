cmake_minimum_required(VERSION 3.20)
project(clickstream_generator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(clickstream-generator
  src/main.cpp
  src/event.cpp
  src/http_client.cpp
  src/kafka_producer.cpp
  src/util_time.cpp
  src/util_csv.cpp
)

target_include_directories(clickstream-generator PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# librdkafka
find_library(RDKAFKA_LIB rdkafka REQUIRED)
target_link_libraries(clickstream-generator PRIVATE ${RDKAFKA_LIB})

target_compile_options(clickstream-generator PRIVATE -Wall -Wextra -Wpedantic)
