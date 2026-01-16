#include "json.hpp"

#include <cctype>
#include <string>
#include <vector>

// NOTE:
// Раньше тут был regex-парсер вида "\{([^}]*)\}", который ломается,
// как только внутри значения строки встречается символ '}' (например props_json:"{...}").
// В результате API переставал принимать события от генератора.
//
// Ниже — очень небольшой ручной парсер, который:
// - корректно выделяет JSON-объекты верхнего уровня в массиве
// - понимает пары key:"value" и key:number
// - корректно обрабатывает экранированные кавычки внутри строк
// Это не «полный JSON», но для нашего контракта достаточно и стабильно.

static inline void skip_ws(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
}

static bool parse_string(const std::string& s, size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size()) {
            char n = s[i++];
            switch (n) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default:
                    // Неизвестный escape — оставим как есть.
                    out.push_back(n);
                    break;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

static bool parse_number_as_string(const std::string& s, size_t& i, std::string& out) {
    size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    bool any = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        any = true;
        ++i;
    }
    if (!any) return false;
    out.assign(s.begin() + static_cast<long>(start), s.begin() + static_cast<long>(i));
    return true;
}

static bool parse_object_kv(const std::string& s, size_t& i, JsonValue& v) {
    // ожидаем начало объекта
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    skip_ws(s, i);
    if (i < s.size() && s[i] == '}') { ++i; return true; }

    while (i < s.size()) {
        skip_ws(s, i);
        std::string key;
        if (!parse_string(s, i, key)) return false;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        skip_ws(s, i);

        std::string value;
        if (i < s.size() && s[i] == '"') {
            if (!parse_string(s, i, value)) return false;
            v.object[key] = value;
        } else {
            // число -> сохраняем как строку
            if (!parse_number_as_string(s, i, value)) {
                // Для простоты игнорируем сложные типы (true/false/null/obj/arr)
                // и просто пропускаем до следующей запятой/конца объекта.
                size_t start = i;
                int depth = 0;
                bool in_str = false;
                while (i < s.size()) {
                    char c = s[i];
                    if (!in_str) {
                        if (c == '"') in_str = true;
                        else if (c == '{' || c == '[') ++depth;
                        else if (c == '}' || c == ']') {
                            if (depth == 0) break;
                            --depth;
                        } else if (c == ',' && depth == 0) break;
                    } else {
                        if (c == '\\') { i += 2; continue; }
                        if (c == '"') in_str = false;
                    }
                    ++i;
                }
                value.assign(s.begin() + static_cast<long>(start), s.begin() + static_cast<long>(i));
                v.object[key] = value;
            } else {
                v.object[key] = value;
            }
        }

        skip_ws(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return false;
    }
    return false;
}

std::vector<JsonValue> parse_json_array(const std::string& body) {
    std::vector<JsonValue> result;
    size_t i = 0;
    skip_ws(body, i);

    // массив может быть как [ {...}, {...} ] так и просто {...} / несколько объектов
    bool in_array = false;
    if (i < body.size() && body[i] == '[') { in_array = true; ++i; }

    while (i < body.size()) {
        skip_ws(body, i);
        if (in_array && i < body.size() && body[i] == ']') break;
        if (i >= body.size()) break;

        if (body[i] != '{') {
            // пропускаем мусор до следующего объекта
            ++i;
            continue;
        }

        JsonValue v;
        size_t start = i;
        if (!parse_object_kv(body, i, v)) {
            // если не распарсили — продвинемся на 1, чтобы не зациклиться
            i = start + 1;
            continue;
        }
        result.push_back(std::move(v));

        skip_ws(body, i);
        if (in_array && i < body.size() && body[i] == ',') ++i;
    }
    return result;
}
