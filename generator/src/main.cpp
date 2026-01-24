#include "event.hpp"
#include "http_client.hpp"
#include "kafka_producer.hpp"
#include "util_csv.hpp"
#include "util_time.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static std::string uuid_v4_from_seed(uint64_t seed) {
  std::mt19937_64 rng(seed ^ 0x9E3779B97F4A7C15ULL);
  auto hex = [](uint64_t x, int n) {
    static const char* h = "0123456789abcdef";
    std::string s(n, '0');
    for (int i = n - 1; i >= 0; --i) {
      s[i] = h[x & 0xFULL];
      x >>= 4;
    }
    return s;
  };

  uint64_t a = rng();
  uint64_t b = rng();
  // Set version=4 (0100) in the time_hi_and_version field.
  a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  // Set variant=1 (10xx) in the clock_seq_hi_and_reserved field.
  b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  // 8-4-4-4-12
  return hex(a >> 32, 8) + "-" +
         hex((a >> 16) & 0xFFFF, 4) + "-" +
         hex(a & 0xFFFF, 4) + "-" +
         hex(b >> 48, 4) + "-" +
         hex(b & 0xFFFFFFFFFFFFULL, 12);
}

struct Args {
  uint64_t count = 1000;     // 0 = infinite
  uint32_t batch = 50;

  std::string http_url;          // http://localhost:8081/events
  std::string kafka_bootstrap;   // localhost:9092
  std::string kafka_topic = "clickstream-events-raw";
  std::string csv_path;          // ./events.csv

  uint32_t rate_eps = 0; // 0 = unlimited

  uint32_t days_back = 7;
};

struct MinioCfg {
  bool enabled = false;
  std::string endpoint;   // http://minio:9000
  std::string access_key;
  std::string secret_key;
  std::string bucket = "click-analysis";
  uint64_t rotate_every_events = 20000; // only for --count 0
};

static const char* getenv_or_null(const char* k) {
  const char* v = std::getenv(k);
  return (v && *v) ? v : nullptr;
}

static std::string shell_quote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

static std::string now_ts() {
  using namespace std::chrono;
  auto sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
  return std::to_string(sec);
}

static uint64_t make_run_salt() {
  // Make each generator run produce a different event stream and different filenames.
  std::random_device rd;
  uint64_t a = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
  uint64_t b = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
  return a ^ (b + 0x9E3779B97F4A7C15ULL);
}

static bool upload_to_minio(const MinioCfg& m, const std::string& local_path, const std::string& object_name, std::string* err) {
  const std::string cmd = "sh -lc " + shell_quote(
    "mc alias set local " + m.endpoint + " " + m.access_key + " " + m.secret_key + " >/dev/null 2>&1 && "
    "mc cp " + local_path + " local/" + m.bucket + "/" + object_name + " >/dev/null 2>&1"
  );
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    if (err) *err = "mc upload failed rc=" + std::to_string(rc);
    return false;
  }
  return true;
}

static bool parseArgs(int argc, char** argv, Args* a) {
  for (int i = 1; i < argc; i++) {
    std::string k = argv[i];
    auto need = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };

    if (k == "--count") a->count = std::stoull(need("--count"));
    else if (k == "--batch") a->batch = static_cast<uint32_t>(std::stoul(need("--batch")));
    else if (k == "--days-back") a->days_back = static_cast<uint32_t>(std::stoul(need("--days-back")));
    else if (k == "--http") a->http_url = need("--http");
    else if (k == "--kafka") a->kafka_bootstrap = need("--kafka");
    else if (k == "--topic") a->kafka_topic = need("--topic");
    else if (k == "--csv") a->csv_path = need("--csv");
    else if (k == "--rate") a->rate_eps = static_cast<uint32_t>(std::stoul(need("--rate")));
    else if (k == "--help") {
      std::cout <<
        "clickstream-generator\n"
        "  --count N        number of events to generate (default 1000), 0 = infinite\n"
        "  --batch N        HTTP batch size (default 50)\n"
        "  --http URL       send batches to HTTP (e.g. http://localhost:8081/events)\n"
        "  --kafka HOSTS    produce JSON to Kafka (e.g. localhost:9092)\n"
        "  --topic NAME     kafka topic (default clickstream-events-raw)\n"
        "  --csv PATH       write dataset to CSV file\n"
        "  --rate EPS       limit generation rate (events per second), 0 = unlimited\n"
        "  --days-back N    spread created_at across last N days (default 7, min 1)\n"
        "\nMinIO auto-upload (enabled via env, even without --csv):\n"
        "  MINIO_ENDPOINT=http://minio:9000\n"
        "  MINIO_ACCESS_KEY=minioadmin\n"
        "  MINIO_SECRET_KEY=minioadmin123\n"
        "  MINIO_BUCKET=click-analysis\n"
        "  MINIO_ROTATE_EVERY=20000   (events per chunk for --count 0)\n";
      return false;
    } else {
      std::cerr << "unknown arg: " << k << " (use --help)\n";
      return false;
    }
  }

  if (a->batch == 0) a->batch = 1;
  if (a->days_back == 0) a->days_back = 1;
  return true;
}

static std::string buildJsonArray(const std::vector<std::string>& items) {
  std::string out;
  out.reserve(items.size() * 256);
  out.push_back('[');
  for (size_t i = 0; i < items.size(); i++) {
    if (i) out.push_back(',');
    out += items[i];
  }
  out.push_back(']');
  return out;
}

int main(int argc, char** argv) {
  Args a;
  if (!parseArgs(argc, argv, &a)) return 1;

  const uint64_t run_salt = make_run_salt();
  std::cerr << "[generator] run_salt=" << run_salt << "\n";

  MinioCfg m;
  if (auto ep = getenv_or_null("MINIO_ENDPOINT")) {
    m.enabled = true;
    m.endpoint = ep;
    m.access_key = getenv_or_null("MINIO_ACCESS_KEY") ? getenv_or_null("MINIO_ACCESS_KEY") : "";
    m.secret_key = getenv_or_null("MINIO_SECRET_KEY") ? getenv_or_null("MINIO_SECRET_KEY") : "";
    if (auto b = getenv_or_null("MINIO_BUCKET")) m.bucket = b;
    if (auto r = getenv_or_null("MINIO_ROTATE_EVERY")) {
      try {
        m.rotate_every_events = std::stoull(r);
      } catch (...) {
        std::cerr << "invalid MINIO_ROTATE_EVERY, using default " << m.rotate_every_events << "\n";
      }
      if (m.rotate_every_events == 0) m.rotate_every_events = 20000;
    }
    if (m.access_key.empty() || m.secret_key.empty()) {
      std::cerr << "MINIO_ENDPOINT is set but MINIO_ACCESS_KEY/MINIO_SECRET_KEY are empty\n";
      return 2;
    }
  }

  // HTTP sink
  Url http{};
  bool http_enabled = false;
  if (!a.http_url.empty()) {
    if (!parseHttpUrl(a.http_url, &http)) {
      std::cerr << "invalid --http url, expected http://host:port/path\n";
      return 2;
    }
    http_enabled = true;
  }

  // Kafka sink
  KafkaProducer kp;
  bool kafka_enabled = false;
  if (!a.kafka_bootstrap.empty()) {
    std::string err;
    if (!kp.start(a.kafka_bootstrap, a.kafka_topic, &err)) {
      std::cerr << "kafka start failed: " << err << "\n";
      return 2;
    }
    kafka_enabled = true;
  }

  // CSV sink
  std::ofstream csv;
  bool csv_enabled = false;
  std::string csv_path = a.csv_path;
  if (csv_path.empty() && m.enabled) {
    csv_path = "/tmp/events.csv";
  }
  if (!csv_path.empty()) {
    csv.open(csv_path, std::ios::out | std::ios::trunc);
    if (!csv) {
      std::cerr << "cannot open csv file: " << csv_path << "\n";
      return 2;
    }
    csv_enabled = true;
    csv << csvHeader() << "\n";
  }

  if (!http_enabled && !kafka_enabled && !csv_enabled) {
    std::cerr << "no sinks enabled: provide at least one of --http / --kafka / --csv, or set MINIO_* env to enable implicit CSV\n";
    return 2;
  }

  const auto start = std::chrono::steady_clock::now();
  auto last_log = start;

  const long long base_epoch = unixNowSeconds();

  uint64_t ok = 0, fail = 0;
  uint64_t produced = 0;

  std::vector<std::string> batch;
  batch.reserve(a.batch);

  auto throttle = [&](uint64_t produced_total) {
    if (a.rate_eps == 0) return;
    using namespace std::chrono;
    const double target_seconds = static_cast<double>(produced_total) / static_cast<double>(a.rate_eps);
    const auto target_time = start + duration_cast<steady_clock::duration>(duration<double>(target_seconds));
    auto now = steady_clock::now();
    if (now < target_time) {
      std::this_thread::sleep_for(target_time - now);
    }
  };

  auto maybe_upload_rotate = [&](bool force) {
    if (!m.enabled || !csv_enabled) return;
    const bool infinite = (a.count == 0);

    if (infinite) {
      if (!force && (produced % m.rotate_every_events != 0)) return;
      csv.flush();
      const std::string obj = "events_contract_v1_" + now_ts() + "_" + std::to_string(produced) + "_" + std::to_string(run_salt) + ".csv";
      std::string err;
      if (!upload_to_minio(m, csv_path, obj, &err)) {
        std::cerr << "[minio] upload failed: " << err << "\n";
        return;
      }
      std::cerr << "[minio] uploaded " << obj << "\n";

      csv.close();
      csv.open(csv_path, std::ios::out | std::ios::trunc);
      csv << csvHeader() << "\n";
      return;
    }

    if (!force) return;
    csv.flush();
    const std::string obj = "events_contract_v1_" + now_ts() + "_" + std::to_string(run_salt) + ".csv";
    std::string err;
    if (!upload_to_minio(m, csv_path, obj, &err)) {
      std::cerr << "[minio] upload failed: " << err << "\n";
    } else {
      std::cerr << "[minio] uploaded " << obj << "\n";
    }
  };

  auto maybe_log = [&]() {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 5) {
      double elapsed = std::chrono::duration<double>(now - start).count();
      double eps = (elapsed > 0.0) ? (produced / elapsed) : 0.0;
      std::cerr << "[generator] produced=" << produced << " ok=" << ok << " fail=" << fail
                << " avg_eps=" << eps << "\n";
      last_log = now;
    }
  };

  auto send_http_batch = [&](bool force) {
    if (!http_enabled) return;
    if (batch.empty()) return;
    if (!force && batch.size() < a.batch) return;

    std::string body = buildJsonArray(batch);
    int status = 0;
    std::string resp, err;
    bool sent = httpPostJson(http, body, &status, &resp, &err);

    if (!sent || status < 200 || status >= 300) {
      fail += batch.size();
    } else {
      ok += batch.size();
    }

    batch.clear();
  };

  const bool infinite = (a.count == 0);
  for (uint64_t i = 1; infinite || (i <= a.count); i++) {
    const uint64_t seq = i ^ (run_salt * 1315423911ULL);
    ClickstreamEvent ev = makeRandomEvent(seq, base_epoch, a.days_back);
    std::string json = eventToJson(ev);
    produced++;

    // CSV output
    if (csv_enabled) {
      // Write CSV strictly by the contract (see events.csv).
      const std::string event_id = uuid_v4_from_seed((seq * 1315423911ULL) ^ run_salt ^ ev.user_id);
      const std::string event_type = ev.type;
      const std::string created_at = ev.created_at;
      const std::string received_at = nowUtcIso8601();
      const std::string user_id_str = std::to_string(ev.user_id);
      const std::string url = ev.url;
      const std::string element_id = "";
      const std::string element_type = "";
      const std::string element_text = "";

      csv << csvJoin({
        event_id,
        event_type,
        created_at,
        received_at,
        ev.session_id,
        user_id_str,
        ev.ip,
        url,
        ev.referrer,
        ev.device_type,
        ev.user_agent,
        element_id,
        element_type,
        element_text,
        ev.payload_json
      }) << "\n";
    }

    // Kafka output (1 msg = 1 event)
    if (kafka_enabled) {
      std::string err;
      if (!kp.send(json, &err)) fail++;
      else ok++;
    }

    // HTTP output (batched as JSON array)
    if (http_enabled) {
      batch.push_back(json);
      send_http_batch(false);
    }

    throttle(produced);
    maybe_upload_rotate(false);
    maybe_log();
  }

  // Final flush
  send_http_batch(true);
  if (kafka_enabled) kp.flush(5000);

  // Final MinIO upload / rotate
  maybe_upload_rotate(true);

  std::cerr << "done. produced=" << produced << " ok=" << ok << " fail=" << fail << "\n";
  return (fail == 0 ? 0 : 3);
}
