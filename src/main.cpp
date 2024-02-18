#include "mqtt.hpp"
#include "bacnet.hpp"
#include <thread>
 
using namespace std;

std::vector<std::string> split_string(const std::string& str, char delimiter) {
    std::vector<std::string> result;

    size_t start = 0;
    size_t end = str.find(delimiter);

    while (end != std::string_view::npos) {
        result.push_back(str.substr(start, end - start));
        start = end + 1;
        end = str.find(delimiter, start);
    }

    result.push_back(str.substr(start));

    return result;
}
 
int main(int argc, char *argv[]) {
    const char* mqtt_host = std::getenv("MQTT_HOST");
    const char* mqtt_port = std::getenv("MQTT_PORT");
    MessageHandler handler(mqtt_host ? mqtt_host : "localhost", mqtt_port ? std::stol(mqtt_port) : 1883);
    bacnet_init([&handler](uint32_t id, const std::string& type, uint32_t instance, std::string value){
        std::string topic = "bacnet-out/" + std::to_string(id) + "/" + type + "/" + std::to_string(instance);
        handler.pub_message(topic, value);
    });
    MessageHandler::add_callback([](const std::string& topic, const std::string& message) {
        auto elements = split_string(topic, '/');
        if (elements.size() != 4) {
            return;
        }
        uint32_t id = std::stoul(elements[1]);
        const std::string& type = elements[2];
        uint32_t instance = std::stoul(elements[3]);
        bacnet_send(id, type, instance, message);
    });
 
    while (true) {
        bacnet_task();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
