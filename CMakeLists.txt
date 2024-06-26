cmake_minimum_required(VERSION 3.16)
PROJECT (bacnet-mqtt)
include(cmake/findmosquitto.cmake)
include(FetchContent)
 
find_mosquitto()

set(BACNET_STACK_BUILD_APPS OFF)
set(BACDL_BIP OFF)
set(BACDL_MSTP ON)
FetchContent_Declare(
  bacnet-stack
  GIT_REPOSITORY https://github.com/bacnet-stack/bacnet-stack.git
  GIT_SHALLOW TRUE
  GIT_TAG bacnet-stack-1.3.4
  )
FetchContent_MakeAvailable(bacnet-stack)
target_compile_definitions(bacnet-stack PUBLIC MSTP_PDU_PACKET_COUNT=128)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${MOSQUITTO_INCLUDE_DIR})

add_executable(bacnet-mqtt src/main.cpp src/bacnet.cpp src/mqtt.cpp)
add_dependencies(bacnet-mqtt bacnet-stack)
target_link_libraries(bacnet-mqtt ${MOSQUITTO_LIBRARIES})
target_link_libraries(bacnet-mqtt bacnet-stack::bacnet-stack)
target_compile_features(bacnet-mqtt PRIVATE cxx_std_20)
target_compile_definitions(bacnet-mqtt PRIVATE BACAPP_PRINT_ENABLED)
