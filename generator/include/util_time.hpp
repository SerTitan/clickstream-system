#pragma once
#include <string>

std::string nowUtcIso8601();

std::string utcIso8601FromUnixSeconds(long long epoch_seconds);

long long unixNowSeconds();
