/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_APP_REGISTRY_H_
#define DESK_HUB_APP_REGISTRY_H_

#include <stddef.h>
#include <zephyr/sys/iterable_sections.h>

/** @brief Static app metadata discovered by the selector through the linker. */
struct desk_hub_app {
	/** Display name; must remain valid for the lifetime of the application. */
	const char *name;
	/** Optional setup; return 0 on success or negative errno to skip on_run. */
	int (*on_entry)(void);
	/** Required event loop; return 0 for the selector or negative errno on failure. */
	int (*on_run)(void);
	/** Optional cleanup, called even when on_entry fails. Must tolerate partial setup. */
	void (*on_exit)(void);
};

/**
 * @brief Register an app for automatic inclusion in the selector.
 *
 * Use once in the app's controller source file. The selector discovers the
 * registration through Zephyr's iterable section. Registration symbols sort
 * lexically and define tile order.
 * @param symbol Unique C identifier; determines the app's position in the menu.
 * @param app_name Display name string with static lifetime.
 * @param app_run Main-thread on_run callback returning 0 or negative errno.
 * @param ... Optional designated hooks: .on_entry = setup, .on_exit = cleanup.
 * All hooks run on the main UI thread. Omitted hooks default to NULL.
 */
#define DESK_HUB_APP_REGISTER(symbol, app_name, app_run, ...)                                      \
	STRUCT_SECTION_ITERABLE(desk_hub_app,                                                      \
				symbol) = {.name = app_name, .on_run = app_run, __VA_ARGS__}

/** @brief Return the number of compiled-in registrations. */
size_t desk_hub_app_count(void);
/** @brief Return a borrowed registration, or NULL if index is out of range. */
const struct desk_hub_app *desk_hub_app_at(size_t index);

#endif /* DESK_HUB_APP_REGISTRY_H_ */
