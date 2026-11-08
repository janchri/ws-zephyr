# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

# CONF_FILE selects the hardware role; MCU selects a simulated bus participant.
set(role bd)
if(CONF_FILE MATCHES "gw.conf" OR MCU STREQUAL "gw")
  set(role gw)
endif()
set(CONF_FILE ${role}.conf)
list(PREPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf)
set(role_overlay ${role}.overlay)

# Include the same board settings with either role, also under sysbuild.
set(board_target "${BOARD}")
if(BOARD_QUALIFIERS AND NOT BOARD MATCHES "/")
  string(APPEND board_target "/${BOARD_QUALIFIERS}")
endif()
string(REPLACE "/" "_" board_config "${board_target}")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/boards/${board_config}.conf")
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/boards/${board_config}.conf)
endif()

if(BOARD MATCHES "^native_sim")
  set(MCU pico CACHE STRING "Simulated MCU: pico, nrf, esp32 or gw")
  list(APPEND DTS_EXTRA_CPPFLAGS -DTWIXT_SELF_LABEL=mcu_${MCU} -DTWIXT_GATEWAY_LABEL=mcu_gw)
  if(role STREQUAL "gw")
    set(role_overlay gw-native.overlay)
  endif()
elseif(BOARD MATCHES "^nrf7002dk" AND role STREQUAL "gw")
  list(APPEND EXTRA_CONF_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ble.conf)
  set(role_overlay gw-ble.overlay)
elseif(BOARD MATCHES "^rpi_pico" AND role STREQUAL "bd")
  list(APPEND DTS_EXTRA_CPPFLAGS -DTWIXT_GATEWAY_LABEL=mcu_nrf)
endif()
list(APPEND EXTRA_DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${role_overlay})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(twixt_system)

target_sources(app PRIVATE src/main.c src/mcu_settings.c src/twixt_add.c)
target_sources_ifdef(CONFIG_TWIXT_USB_GATEWAY app PRIVATE src/usb.c)
target_sources_ifdef(CONFIG_BT app PRIVATE src/smp_proxy.c)
target_sources_ifdef(CONFIG_TWIXT_RP2040_WATCHDOG app PRIVATE src/watchdog.c)
