cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(temperature_monitor)

target_sources(app PRIVATE
    src/network/w1_ds18b20.c
    src/network/wifi.c
    src/network/mqtt_conn.c
    src/main.c)