#include "mqtt.hpp"
#include "bacnet.hpp"
#include <thread>
 
using namespace std;

namespace mesg {

}
 
int main(int argc, char *argv[]) {
    MessageHandler handle("192.168.1.105", 1883);
    bacnet_init([&handle](uint32_t id, uint32_t type, uint32_t instance, std::string value){
        std::string topic = "bacnet-out/" + std::to_string(id) + "/" + std::to_string(type) + "/" + std::to_string(instance);
        handle.pub_message(topic, value);
    });
 
    while (true) {
        bacnet_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
