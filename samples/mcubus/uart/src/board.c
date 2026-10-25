/*
 * Copyright (c) 2026 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * A board on the bus: its shell is on a UART over the bus to the gateway
 * (board.overlay), with the "mcubus" commands. Its prompt is its name.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell_uart.h>

#include <mcubus/mcubus.h>

LOG_MODULE_REGISTER(board, LOG_LEVEL_INF);

int main(void)
{
	static char prompt[32];

	snprintf(prompt, sizeof(prompt), "%s:~$ ", mcubus_name());
	shell_prompt_change(shell_backend_uart_get_ptr(), prompt);

	LOG_INF("%s at 0x%02x, shell on the bus", mcubus_name(), mcubus_addr());
	return 0;
}
