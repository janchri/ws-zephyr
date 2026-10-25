/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MCUmgr over the mcubus: the board and the gateway are configuration only
 * (board.conf, board.overlay, gateway.overlay). This says which one runs.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <mcubus/mcubus.h>

LOG_MODULE_REGISTER(smp_sample, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("%s at 0x%02x: %s", mcubus_name(), mcubus_addr(),
		IS_ENABLED(CONFIG_MCUMGR) ? "MCUmgr on the shell" : "gateway");
	return 0;
}
