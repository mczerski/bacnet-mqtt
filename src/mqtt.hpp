#pragma once
#include <string>
#include <functional>
#include <mosquitto.h>

class MessageHandler {
public:
    MessageHandler(const std::string& host, const int port);
    ~MessageHandler();
    static int call_back_func(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg);
    int pub_message(const std::string& topic, const std::string& message);
    static void add_callback(std::function<void(const std::string&, const std::string&)> callback);

private:
    struct mosquitto* _mosq = nullptr;
    std::string _host;
    int _port;
    int _keep_alive = 60;
    bool _connected = false;
    static std::function<void(const std::string&, const std::string&)> callback_;
    void init_();
};

