/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_CONWAY_H
#define DESK_HUB_CONWAY_H

/**
 * @brief Run Conway's event loop after its registered on_entry hook succeeds.
 *
 * @return 0 when returning to the menu. The runtime invokes on_exit for cleanup.
 */
int desk_hub_conway_run(void);

#endif /* DESK_HUB_CONWAY_H */
