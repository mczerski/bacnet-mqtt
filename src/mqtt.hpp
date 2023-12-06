#pragma once
#include <string>
#include <mosquitto.h>

class MessageHandler {
public:
    MessageHandler(const std::string& host, const int port);
    ~MessageHandler();
    static int call_back_func(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg);
    int pub_message(const std::string& topic, const std::string& message);

private:
    struct mosquitto* _mosq = nullptr;
    std::string _host;
    int _port;
    int _keep_alive = 60;
    bool _connected = false;
    void init_();
};

