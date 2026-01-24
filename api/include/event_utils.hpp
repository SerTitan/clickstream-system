#pragma once
#include <string>
#include <map>
#include <unordered_set>

namespace event_utils {

inline const std::unordered_set<std::string>& allowed_event_types() {
    static const std::unordered_set<std::string> types = {
        "click", "view", "purchase", "signup", "login", "logout"
    };
    return types;
}

inline std::string normalize_event_type(std::string t) {
    for (char& c : t) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (t == "page_view" || t == "pageview" || t == "view_page") t = "view";
    if (t == "sign_up" || t == "register" || t == "registration") t = "signup";
    if (t == "sign_in") t = "login";
    if (t == "sign_out") t = "logout";
    return t;
}

inline std::string trim_ws(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline bool looks_like_json_text(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    auto e = s.find_last_not_of(" \t\r\n");
    if (e == std::string::npos || e <= b) return false;
    return (s[b] == '{' && s[e] == '}') || (s[b] == '[' && s[e] == ']');
}

inline std::string unescape_backslash_json(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            switch (n) {
                case '\\': out.push_back('\\'); ++i; continue;
                case '"':  out.push_back('"');  ++i; continue;
                case 'n':  out.push_back('\n'); ++i; continue;
                case 'r':  out.push_back('\r'); ++i; continue;
                case 't':  out.push_back('\t'); ++i; continue;
                default: break;
            }
        }
        out.push_back(c);
    }
    return out;
}

inline std::string get_or(const std::map<std::string, std::string>& m,
                          const std::string& key, const std::string& def) {
    auto it = m.find(key);
    return (it == m.end()) ? def : it->second;
}

inline std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

inline const std::unordered_set<std::string>& allowed_keys() {
    static const std::unordered_set<std::string> keys = {
        "event_id", "event_type", "type", "created_at", "received_at",
        "session_id", "user_id", "ip", "url", "page_url", "referrer",
        "device_type", "user_agent", "payload", "props_json", "payload_json", "device_id"
    };
    return keys;
}

} // namespace event_utils
