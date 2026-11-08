# SPDX-License-Identifier: Apache-2.0
# The nRF gateway uses the network core as its BLE controller.
if(BOARD_QUALIFIERS STREQUAL "nrf5340/cpuapp")
  if("${twixt-system_CONF_FILE};${CONF_FILE}" MATCHES "gw.conf")
    string(REPLACE "/cpuapp" "/cpunet" net_board "${BOARD}/${BOARD_QUALIFIERS}")
    ExternalZephyrProject_Add(
      APPLICATION hci_ipc
      SOURCE_DIR ${ZEPHYR_BASE}/samples/bluetooth/hci_ipc
      BOARD ${net_board}
    )
    set(hci_ipc_EXTRA_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/hci_ipc.conf CACHE INTERNAL "")
  endif()
endif()
