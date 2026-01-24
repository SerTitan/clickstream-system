#include "event.hpp"
#include "util_time.hpp"
#include <random>
#include <sstream>
#include <vector>

static std::string pick(const std::vector<std::string>& v, std::mt19937_64& rng) {
  std::uniform_int_distribution<size_t> d(0, v.size() - 1);
  return v[d(rng)];
}

static std::string jsonEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 16);
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) out.push_back('?');
        else out.push_back(static_cast<char>(c));
        break;
    }
  }
  return out;
}

static std::string generateIp(std::mt19937_64& rng) {
  std::uniform_int_distribution<int> octet(1, 254);
  return std::to_string(octet(rng)) + "." + std::to_string(octet(rng)) + "." +
         std::to_string(octet(rng)) + "." + std::to_string(octet(rng));
}

ClickstreamEvent makeRandomEvent(uint64_t seq, long long base_epoch_seconds, uint32_t days_back) {
  std::mt19937_64 rng(seq * 1315423911ULL + 0x9E3779B97F4A7C15ULL);

  std::vector<std::string> types = {
    "view", "view", "view", "view", "view",  // 50% views
    "click", "click", "click",               // 30% clicks
    "login", "logout",                       // 10% auth
    "signup", "purchase"                     // 10% conversions
  };

  std::vector<std::string> urls = {
    "/", "/home",
    "/catalog", "/catalog?category=electronics", "/catalog?category=clothing", "/catalog?category=home",
    "/product/1", "/product/2", "/product/42", "/product/100", "/product/256", "/product/999",
    "/cart", "/checkout", "/checkout/payment", "/checkout/confirm",
    "/account", "/account/orders", "/account/settings",
    "/search?q=phone", "/search?q=laptop", "/search?q=headphones",
    "/about", "/contact", "/help", "/faq"
  };

  std::vector<std::string> desktop_uas = {
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 11.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:123.0) Gecko/20100101 Firefox/123.0",
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_3) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Safari/605.1.15",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:123.0) Gecko/20100101 Firefox/123.0"
  };

  std::vector<std::string> mobile_uas = {
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 16_7 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 17_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/122.0.6261.62 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (Linux; Android 14; Pixel 8 Pro) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.6261.64 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 14; SM-S928B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.6261.64 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 13; SM-A546B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.6167.178 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 14; 2312DRA50G) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.6261.64 Mobile Safari/537.36",
    "Mozilla/5.0 (Linux; Android 13; Redmi Note 12 Pro) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.6167.178 Mobile Safari/537.36"
  };

  std::vector<std::string> tablet_uas = {
    "Mozilla/5.0 (iPad; CPU OS 17_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.2 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (iPad; CPU OS 16_7 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (iPad; CPU OS 17_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/122.0.6261.62 Mobile/15E148 Safari/604.1",
    "Mozilla/5.0 (Linux; Android 14; SM-X910) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.6261.64 Safari/537.36",
    "Mozilla/5.0 (Linux; Android 13; SM-X710) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.6167.178 Safari/537.36",
    "Mozilla/5.0 (Linux; Android 14; 23043RP34G) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.6261.64 Safari/537.36",
    "Mozilla/5.0 (Linux; Android 13; TB350FU) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/121.0.6167.178 Safari/537.36"
  };

  std::vector<std::string> referrers = {
    "",  // direct traffic
    "https://google.com/search?q=products",
    "https://google.com/search?q=best+deals",
    "https://yandex.ru/search/?text=shop",
    "https://facebook.com",
    "https://instagram.com",
    "https://twitter.com",
    "https://t.me/channel",
    "https://vk.com",
    "https://youtube.com/watch?v=abc123",
    "https://mail.google.com",
    "https://news.ycombinator.com"
  };

  std::vector<std::string> element_ids = {
    "#add-to-cart", "#buy-now", "#submit-button", "#checkout-btn",
    "#nav-home", "#nav-catalog", "#nav-cart", "#nav-account",
    "#search-input", "#search-btn", "#filter-apply",
    "#product-image", "#product-details", "#reviews-tab",
    "#login-btn", "#signup-btn", "#logout-btn",
    "#banner-promo", "#featured-item", "#newsletter-subscribe"
  };

  std::vector<std::string> event_titles = {
    "page_load", "button_click", "form_submit", "navigation",
    "add_to_cart", "remove_from_cart", "checkout_start", "checkout_complete",
    "search", "filter_apply", "sort_change",
    "product_view", "product_zoom", "review_read",
    "login_attempt", "signup_attempt", "logout",
    "promo_click", "banner_view", "newsletter_signup"
  };

  std::vector<std::string> countries = {
    "RU", "RU", "RU", "RU",  // 40% Russia
    "US", "US",              // 20% USA
    "DE", "GB", "FR",        // 15% Europe
    "KZ", "BY", "UA",        // 15% CIS
    "CN", "JP"               // 10% Asia
  };

  std::uniform_int_distribution<uint64_t> userDist(1000, 5000);
  std::uniform_int_distribution<int> xy(0, 1920);
  std::uniform_int_distribution<int> y_coord(0, 1080);

  ClickstreamEvent e;
  e.type = pick(types, rng);
  e.user_id = userDist(rng);

  std::uniform_int_distribution<int> sessionNum(1, 10);
  e.session_id = "sess-" + std::to_string(e.user_id) + "-" + std::to_string(sessionNum(rng));

  e.url = pick(urls, rng);
  e.referrer = pick(referrers, rng);
  e.country = pick(countries, rng);
  e.ip = generateIp(rng);

  std::uniform_int_distribution<int> deviceDist(1, 100);
  int deviceRoll = deviceDist(rng);
  if (deviceRoll <= 50) {
    e.device_type = "desktop";
    e.user_agent = pick(desktop_uas, rng);
  } else if (deviceRoll <= 90) {
    e.device_type = "mobile";
    e.user_agent = pick(mobile_uas, rng);
  } else {
    e.device_type = "tablet";
    e.user_agent = pick(tablet_uas, rng);
  }

  if (days_back == 0) days_back = 1;
  const long long max_span = static_cast<long long>(days_back) * 86400LL;
  std::uniform_int_distribution<long long> off(0, (max_span > 0 ? max_span - 1 : 0));
  const long long created_epoch = base_epoch_seconds - off(rng);
  e.created_at = utcIso8601FromUnixSeconds(created_epoch);

  std::string event_title = pick(event_titles, rng);
  std::string element_id = pick(element_ids, rng);

  if (e.type == "click") {
    std::vector<std::string> click_titles = {"button_click", "add_to_cart", "navigation", "promo_click"};
    event_title = pick(click_titles, rng);
  } else if (e.type == "view") {
    std::vector<std::string> view_titles = {"page_load", "product_view", "banner_view"};
    event_title = pick(view_titles, rng);
  } else if (e.type == "purchase") {
    event_title = "checkout_complete";
    element_id = "#checkout-btn";
  } else if (e.type == "signup") {
    event_title = "signup_attempt";
    element_id = "#signup-btn";
  } else if (e.type == "login") {
    event_title = "login_attempt";
    element_id = "#login-btn";
  } else if (e.type == "logout") {
    event_title = "logout";
    element_id = "#logout-btn";
  }

  std::ostringstream payload;
  payload << "{"
          << "\"event_title\":\"" << event_title << "\","
          << "\"element_id\":\"" << element_id << "\","
          << "\"x\":" << xy(rng) << ","
          << "\"y\":" << y_coord(rng)
          << "}";

  e.payload_json = payload.str();
  return e;
}

std::string eventToJson(const ClickstreamEvent& e) {
  std::ostringstream oss;
  const std::string user_id_str = std::to_string(e.user_id);
  const std::string device_id = "dev-" + user_id_str;
  const std::string received_at = nowUtcIso8601();

  oss << "{"
      << "\"type\":\"" << jsonEscape(e.type) << "\","
      << "\"url\":\"" << jsonEscape(e.url) << "\","
      << "\"event_type\":\"" << jsonEscape(e.type) << "\","
      << "\"page_url\":\"" << jsonEscape(e.url) << "\","
      << "\"session_id\":\"" << jsonEscape(e.session_id) << "\","
      << "\"user_id\":\"" << user_id_str << "\","
      << "\"device_id\":\"" << jsonEscape(device_id) << "\","
      << "\"device_type\":\"" << jsonEscape(e.device_type) << "\","
      << "\"referrer\":\"" << jsonEscape(e.referrer) << "\","
      << "\"ip\":\"" << jsonEscape(e.ip) << "\","
      << "\"created_at\":\"" << jsonEscape(e.created_at) << "\","
      << "\"received_at\":\"" << jsonEscape(received_at) << "\","
      << "\"user_agent\":\"" << jsonEscape(e.user_agent) << "\","
      << "\"payload\":" << e.payload_json
      << "}";
  return oss.str();
}

std::string csvHeader() {
  return "event_id,event_type,created_at,received_at,session_id,user_id,ip,url,referrer,device_type,user_agent,element_id,element_type,element_text,payload_json";
}
