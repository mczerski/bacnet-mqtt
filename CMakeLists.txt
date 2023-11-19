cmake_minimum_required(VERSION 3.22)
PROJECT (bacnet-mqtt)
include(cmake/findmosquitto.cmake)
include(ExternalProject)
 
find_mosquitto()

ExternalProject_Add(
  bacnet-stack
  GIT_REPOSITORY https://github.com/bacnet-stack/bacnet-stack.git
  GIT_SHALLOW TRUE
  GIT_TAG master
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX:PATH=<INSTALL_DIR> -DBACNET_STACK_BUILD_APPS=OFF -DBACDL_BIP=OFF -DBACDL_MSTP=ON
  UPDATE_COMMAND ""
  )
ExternalProject_Get_Property(bacnet-stack install_dir)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${MOSQUITTO_INCLUDE_DIR})
include_directories(${install_dir}/include)
add_definitions(-DBACDL_MSTP=1)

ADD_EXECUTABLE(bacnet-mqtt src/main.cpp src/bacnet.cpp)
add_dependencies(bacnet-mqtt bacnet-stack)
target_link_libraries(bacnet-mqtt ${MOSQUITTO_LIBRARIES})
target_link_libraries(bacnet-mqtt ${install_dir}/lib/libbacnet-stack.a)
