#include "util_time.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

static std::tm gmtime_utc(std::time_t tt) {
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  return tm;
}

long long unixNowSeconds() {
  using namespace std::chrono;
  return static_cast<long long>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

std::string utcIso8601FromUnixSeconds(long long epoch_seconds) {
  std::time_t tt = static_cast<std::time_t>(epoch_seconds);
  std::tm tm = gmtime_utc(tt);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string nowUtcIso8601() {
  return utcIso8601FromUnixSeconds(unixNowSeconds());
}
