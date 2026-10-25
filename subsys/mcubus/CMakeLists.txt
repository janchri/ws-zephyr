# Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(mcubus.c)
zephyr_library_sources_ifdef(CONFIG_MCUBUS_SETTINGS settings.c)
zephyr_linker_sources(SECTIONS mcubus_services.ld)
