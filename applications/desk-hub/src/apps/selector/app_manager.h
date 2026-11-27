/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DESK_HUB_APP_MANAGER_H_
#define DESK_HUB_APP_MANAGER_H_

/**
 * @brief Run the selector and registered controllers on the main thread.
 * @pre Platform display, input and app UI are initialized.
 * @return Negative errno on selector failure; otherwise does not return.
 */
int desk_hub_app_manager_run(void);

#endif /* DESK_HUB_APP_MANAGER_H_ */
