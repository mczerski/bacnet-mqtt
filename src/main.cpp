#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mosquitto.h>
 
using namespace std;

namespace mesg {

class MessageHandler {
public:
    MessageHandler(const std::string& host, const int port)
        : _host(host)
        , _port(port)
    {}

    ~MessageHandler() {
        if (_connected) {
            mosquitto_lib_cleanup();
            mosquitto_destroy(_mosq);
        }
    }
    void init() {
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

        auto register_callback = [=] {
            mosquitto_subscribe_callback(
                &MessageHandler::call_back_func, NULL,
                "MessageHandler/#", 0,
                _host.c_str(), _port,
                NULL, 60, true,
                NULL, NULL,
                NULL, NULL);
            // this api can not return??
        };
        std::thread register_callback_thread(register_callback);
        register_callback_thread.detach();
        std::cout << "finished callback init" << std::endl;
    }

    static int call_back_func(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg) {
        std::cout << "FROM topic: " << msg->topic << std::endl;
        std::string message(reinterpret_cast<char *>(msg->payload), msg->payloadlen);
        std::cout << "GOT message:\n" << message << std::endl;
        return 0;
    }

    int pub_message(int idx, const std::string& m) {

        if (mosquitto_publish(_mosq, nullptr, _mesg_pub_topic.c_str(),
                          static_cast<int>(m.length()) + 1,
                          m.c_str(), 0, 0) != MOSQ_ERR_SUCCESS) {

            std::cerr  << "MQTT publish error." << std::endl;
            return 1;
        }
        return 0;
    }

private:
    struct mosquitto* _mosq = nullptr;
    std::string _host;
    int _port;
    int _keep_alive = 60;
    bool _connected = false;
    std::string _mesg_pub_topic = "MessageHandler/publish";
    std::string _mesg_cb_topic = "MessageHandler/call_back";
};
}
 
int main(int argc, char *argv[]) {
    void bacnet_init();
    void bacnet_task();

    bacnet_init();
    //mesg::MessageHandler handle("192.168.1.105", 1883);
    //std::cout << "start init" << std::endl;
    //handle.init();
 
    while (true) {
        bacnet_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
