#!/bin/bash
export LTSMIN_VERSION=$(grep "PACKAGE_VERSION" src/hre/config.h | cut -d" " -f3 | \
    cut -d\" -f2)
