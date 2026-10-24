cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirbelwind)

target_include_directories(app PRIVATE src lib/minimp3)

target_sources(app PRIVATE
    src/main.c
    src/events.c
    src/storage.c
    src/playlist.c
    src/resume.c
    src/player.c
    src/controller.c
    src/inputs.c
    src/shell.c)

# minimp3 mixes float and double constants
set_source_files_properties(src/player.c PROPERTIES COMPILE_OPTIONS -Wno-double-promotion)

# native_sim: simulated hardware and host audio output
target_sources_ifdef(CONFIG_WW_SIM app PRIVATE src/native/sim.c)
if(CONFIG_WW_AUDIO_NATIVE)
  target_sources(app PRIVATE src/native/audio.c)
  # Runs on the Linux side of native_sim (host libc), see native_simulator docs
  target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/native/audio_bottom.c)
endif()

# RFID reader
target_sources_ifdef(CONFIG_WW_RFID app PRIVATE
    src/rfid/mfrc522.c
    src/rfid/reader.c)
target_sources_ifdef(CONFIG_WW_RFID_EMUL app PRIVATE src/native/mfrc522_emul.c)

# ESP32-A1S: I2S + AC101 codec
target_sources_ifdef(CONFIG_WW_AUDIO_AC101 app PRIVATE
    src/resample.c
    src/esp32a1s/ac101.c
    src/esp32a1s/audio.c)

target_sources_ifdef(CONFIG_WW_WIFI_AP app PRIVATE src/wifi.c)

if(CONFIG_WW_HTTP)
  target_sources(app PRIVATE src/http.c)
  set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated)
  zephyr_include_directories(${gen_dir})
  generate_inc_file_for_target(app web/index.html ${gen_dir}/index.html.gz.inc --gzip)
  # Linker section for the resources of the "ww_http" service
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/http_sections.ld
    "#include <zephyr/linker/iterable_sections.h>\n"
    "ITERABLE_SECTION_ROM(http_resource_desc_ww_http, Z_LINK_ITERABLE_SUBALIGN)\n")
  zephyr_linker_sources(SECTIONS ${CMAKE_CURRENT_BINARY_DIR}/http_sections.ld)
endif()
