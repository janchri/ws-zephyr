# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_I2C_MM_EMUL)
  zephyr_library()
  zephyr_library_sources(i2c_mm_emul.c)
  # Linux side of the shared bus (host libc), see native_simulator docs
  target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/i2c_mm_host.c)
endif()
