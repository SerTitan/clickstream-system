cmake_minimum_required(VERSION 3.16)
project(clickstream_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For coverage reporting (optional, use with -DCOVERAGE=ON)
option(COVERAGE "Enable coverage reporting" OFF)

if(COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../api/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../generator/include)

# API source files (excluding main.cpp)
set(API_SOURCES
    ../api/src/json.cpp
    ../api/src/http_request.cpp
    ../api/src/http_response.cpp
    ../api/src/metrics.cpp
    ../api/src/event_handler.cpp
    ../api/src/clickhouse_client.cpp
)

# Generator source files (excluding main.cpp)
set(GENERATOR_SOURCES
    ../generator/src/event.cpp
    ../generator/src/util_time.cpp
    ../generator/src/util_csv.cpp
)

# Test executable
add_executable(tests
    main.cpp
    ${API_SOURCES}
    ${GENERATOR_SOURCES}
)

target_link_libraries(tests pthread)

# Enable testing
enable_testing()
add_test(NAME clickstream_tests COMMAND tests)

# Custom target to run tests
add_custom_target(run_tests
    COMMAND tests
    DEPENDS tests
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running test suite..."
)

# Custom target for coverage report (requires lcov)
if(COVERAGE)
    add_custom_target(coverage
        COMMAND lcov --capture --directory . --output-file coverage.info
        COMMAND lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage_filtered.info
        COMMAND genhtml coverage_filtered.info --output-directory coverage_report
        COMMAND echo "Coverage report generated in coverage_report/index.html"
        DEPENDS run_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Generating coverage report..."
    )
endif()
