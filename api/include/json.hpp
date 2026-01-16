#pragma once
#include <string>
#include <vector>
#include <map>

struct JsonValue {
    std::map<std::string, std::string> object;
};

std::vector<JsonValue> parse_json_array(const std::string& body);
