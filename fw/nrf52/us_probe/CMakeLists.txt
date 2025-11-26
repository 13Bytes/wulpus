#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(us_probe)

# NORDIC SDK APP START
target_sources(app PRIVATE
    src/main.c
    src/ble.c
    src/mesh.c
    src/spi.c
    src/helper.c
    src/testfunctions.c
)
target_include_directories(app PRIVATE include)
# NORDIC SDK APP END
