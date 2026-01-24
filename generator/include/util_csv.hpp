#pragma once
#include <string>
#include <vector>

std::string csvEscape(const std::string& s);
std::string csvJoin(const std::vector<std::string>& cols);
