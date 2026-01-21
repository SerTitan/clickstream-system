#include "test_framework.hpp"

// Include all test files - they register tests automatically
#include "test_api_json.cpp"
#include "test_api_http.cpp"
#include "test_api_metrics.cpp"
#include "test_generator_event.cpp"
#include "test_generator_utils.cpp"
#include "test_clickhouse_client.cpp"
#include "test_event_handler.cpp"

int main() {
    std::cout << "===========================================\n";
    std::cout << " Clickstream System Test Suite\n";
    std::cout << "===========================================\n\n";

    return test::run_all_tests();
}
