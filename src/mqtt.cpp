#include "mqtt.hpp"
#include <iostream>
#include <thread>

MessageHandler::MessageHandler(const std::string& host, const int port)
    : _host(host)
    , _port(port)
{
    init_();
}

MessageHandler::~MessageHandler() {
    if (_connected) {
        mosquitto_lib_cleanup();
        mosquitto_destroy(_mosq);
    }
}
void MessageHandler::init_() {
    // init publish struct
    _mosq = mosquitto_new(nullptr, true, nullptr);
    if (mosquitto_connect(_mosq, _host.c_str(), _port, _keep_alive) != MOSQ_ERR_SUCCESS) {
        std::cerr << "connect error!!" << std::endl;
        _connected = false;
        exit(-1);
    } else {
        _connected = true;
    }
    std::cout << "publish init finished" << std::endl;

    // init recive callback
    mosquitto_lib_init(); 

    auto register_callback = [this] {
        mosquitto_subscribe_callback(
            &MessageHandler::call_back_func,
            NULL,
            "bacnet-in/#",
            0,
            _host.c_str(),
            _port,
            NULL,
            60,
            true,
            NULL,
            NULL,
            NULL,
            NULL
        );
        // this api can not return??
    };
    std::thread register_callback_thread(register_callback);
    register_callback_thread.detach();
    std::cout << "finished callback init" << std::endl;
}

int MessageHandler::call_back_func(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
    std::cout << "FROM topic: " << msg->topic << std::endl;
    std::string message(reinterpret_cast<char *>(msg->payload), msg->payloadlen);
    std::cout << "GOT message: " << message << std::endl;
    callback_(msg->topic, message);
    return 0;
}

int MessageHandler::pub_message(const std::string& topic, const std::string& message) {
    if (mosquitto_publish(_mosq, nullptr, topic.c_str(),
                      static_cast<int>(message.length()) + 1,
                      message.c_str(), 0, 0) != MOSQ_ERR_SUCCESS) {

        std::cerr  << "MQTT publish error." << std::endl;
        return 1;
    }
    return 0;
}

std::function<void(const std::string&, const std::string&)> MessageHandler::callback_;
void MessageHandler::add_callback(std::function<void(const std::string&, const std::string&)> callback) {
    callback_ = callback;
}
