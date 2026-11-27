cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(desk_hub)

zephyr_linker_sources(DATA_SECTIONS src/sections-ram.ld)
zephyr_iterable_section(NAME desk_hub_app GROUP DATA_REGION ${XIP_ALIGN_WITH_INPUT})

target_include_directories(app PRIVATE src/core src/platform src/apps/selector)
target_sources(app PRIVATE
  src/main.c
  src/core/app_registry.c
  src/core/app_runtime.c
  src/core/app_ui.c
  src/core/event.c
  src/platform/clock.c
  src/platform/display_backend.c
  src/platform/gpio_buttons.c
  src/apps/selector/app_manager.c
  src/apps/selector/app_selector_model.c
  src/apps/selector/app_selector_view.c
)
target_sources_ifdef(CONFIG_DESK_HUB_CONWAY app PRIVATE
  src/apps/conway/conway.c
  src/apps/conway/conway_model.c
  src/apps/conway/conway_view.c
)
target_sources_ifdef(CONFIG_DESK_HUB_CALENDAR app PRIVATE
  src/apps/calendar/calendar.c
  src/apps/calendar/calendar_model.c
  src/apps/calendar/calendar_view.c
)
target_sources_ifdef(CONFIG_DESK_HUB_EMULATOR app PRIVATE src/platform/emulator.c)

if(CONFIG_DESK_HUB_EMULATOR)
  target_sources(native_simulator INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/emulator_window.c)
endif()
