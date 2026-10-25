# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# One image per MCU: NODE picks the node of boards/native_sim.overlay this image is
set(NODE a CACHE STRING "MCU to build: a, b or c")
if(NOT NODE MATCHES "^[abc]$")
  message(FATAL_ERROR "NODE must be a, b or c, not ${NODE}")
endif()
list(APPEND DTS_EXTRA_CPPFLAGS -DMCUBUS_SELF_LABEL=mcu_${NODE})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mcubus_settings)

target_sources(app PRIVATE src/main.c)
