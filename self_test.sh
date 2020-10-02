#!/usr/bin/env bash

mkdir -p build && \
cd build && \
cmake .. && \
make self_test_rstd && \
./self_test_rstd $@