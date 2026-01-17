#include "util_csv.hpp"

std::string csvEscape(const std::string& s) {
  bool needQuotes = false;
  for (char c : s) {
    if (c == '"' || c == ',' || c == '\n' || c == '\r') {
      needQuotes = true;
      break;
    }
  }
  if (!needQuotes) return s;

  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (char c : s) {
    if (c == '"') out += "\"\"";
    else out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string csvJoin(const std::vector<std::string>& cols) {
  std::string out;
  for (size_t i = 0; i < cols.size(); i++) {
    if (i) out.push_back(',');
    out += csvEscape(cols[i]);
  }
  return out;
}
