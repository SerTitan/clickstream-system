#pragma once
#include <string>
#include <vector>

class IClickHouseClient {
public:
    virtual ~IClickHouseClient() = default;
    virtual bool insert_raw_event(const std::string& json_each_row, std::string* err_out) = 0;
};

class ClickHouseClient : public IClickHouseClient {
public:
    ClickHouseClient(std::string host, int port,
                     std::string db,
                     std::string user,
                     std::string password,
                     std::string table);

    bool insert_raw_event(const std::string& json_each_row, std::string* err_out) override;

private:
    std::vector<std::string> urls_;
    size_t current_ = 0;

    std::string user_;
    std::string password_;

    static std::vector<std::string> split_csv(const std::string& s);
    static std::string trim(const std::string& s);
    static std::string normalize_base_url(std::string u);
    static std::string build_insert_url(const std::string& base_url,
                                        const std::string& db,
                                        const std::string& table);

    static bool is_retryable_network_error(const std::string& curl_output);
    static std::string shell_escape_single_quotes(const std::string& s);

    bool try_insert_once(const std::string& url, const std::string& json_each_row,
                         std::string* out);
};
