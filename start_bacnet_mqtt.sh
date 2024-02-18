#!/usr/bin/env bash

cd $(dirname $0)
BACNET_IFACE=/tmp/ttyUSB0.1 BACNET_MAX_MASTER=3 BACNET_MSTP_MAC=2 BACNET_DEBUG=1 MQTT_HOST=worcella341.lan ./build/bacnet-mqtt
