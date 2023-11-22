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
  GIT_TAG master
  )
FetchContent_MakeAvailable(bacnet-stack)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${MOSQUITTO_INCLUDE_DIR})

add_executable(bacnet-mqtt src/main.cpp src/bacnet.cpp)
add_dependencies(bacnet-mqtt bacnet-stack)
target_link_libraries(bacnet-mqtt ${MOSQUITTO_LIBRARIES})
target_link_libraries(bacnet-mqtt bacnet-stack::bacnet-stack)
