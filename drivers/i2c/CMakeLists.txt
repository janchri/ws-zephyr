# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_I2C_MM OR CONFIG_I2C_MM_EMUL)
  zephyr_library()
  zephyr_library_sources_ifdef(CONFIG_I2C_MM i2c_mm.c)
  zephyr_library_sources_ifdef(CONFIG_I2C_MM_EMUL i2c_mm_emul.c)
endif()

if(CONFIG_I2C_MM_EMUL)
  # Linux side of the shared bus (host libc), see native_simulator docs
  target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/i2c_mm_host.c)
endif()
