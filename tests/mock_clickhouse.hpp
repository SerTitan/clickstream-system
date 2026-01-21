#pragma once
#include "clickhouse_client.hpp"
#include <string>
#include <vector>
#include <functional>

class MockClickHouseClient : public IClickHouseClient {
public:
    bool insert_result = true;
    std::string insert_error = "";
    std::vector<std::string> inserted_rows;
    int insert_call_count = 0;
    std::function<bool(const std::string&)> insert_callback;

    bool insert_raw_event(const std::string& json, std::string* err_out) override {
        insert_call_count++;
        inserted_rows.push_back(json);

        if (insert_callback) {
            return insert_callback(json);
        }

        if (!insert_result && err_out) {
            *err_out = insert_error;
        }
        return insert_result;
    }

    void reset() {
        insert_result = true;
        insert_error = "";
        inserted_rows.clear();
        insert_call_count = 0;
        insert_callback = nullptr;
    }
};
