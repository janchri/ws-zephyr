cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wirbelwind)

target_include_directories(app PRIVATE src lib/minimp3)

target_sources(app PRIVATE
    src/main.c
    src/controller.c
    src/playlist.c
    src/player.c
    src/shell.c)

# minimp3 mixes float and double constants
set_source_files_properties(src/player.c PROPERTIES COMPILE_OPTIONS -Wno-double-promotion)

if(CONFIG_WW_AUDIO_NATIVE)
  target_sources(app PRIVATE src/audio_native.c)
  # Runs on the host side of native_sim (host libc), see native_simulator docs
  target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_native_bottom.c)
endif()

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
