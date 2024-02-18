#!/usr/bin/env bash

cd $(dirname $0)
mkdir -p build
cmake --build build
