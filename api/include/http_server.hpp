#pragma once
#include <functional>
#include <map>
#include <string>

class HttpRequest;
class HttpResponse;

class HttpServer {
public:
    using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

    explicit HttpServer(int port);
    void add_route(const std::string& method,
                   const std::string& path,
                   Handler handler);
    void start();

private:
    int port_;
    std::map<std::string, Handler> routes_;
};
