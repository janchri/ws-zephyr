/*
 * Copyright (c) 2026 Christoph Jans
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The hardware watchdog reboots a hung gateway, which then logs it and keeps it for "gw_reset".
 * Watchdog scratch registers 0 and 1 survive the reset (the driver and the bootrom use 4-7).
 */

#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <hardware/structs/watchdog.h>

LOG_MODULE_REGISTER(gw_watchdog, LOG_LEVEL_INF);

#define TIMEOUT_MS  2000
#define FEED_MS     500
#define SOFT_REBOOT 0x7b1cffffU
#define MARK        0 /* SOFT_REBOOT before a reboot by software */
#define FED_MS      1 /* uptime of the last feed */

static const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static bool by_watchdog;
static uint32_t fed_ms;

/* A reboot by software leaves WATCHDOG REASON as it was: mark it, so that it does not count */
void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);
	(void)irq_lock();
	watchdog_hw->scratch[MARK] = SOFT_REBOOT;
	NVIC_SystemReset();
}

static int read_reset(void)
{
	by_watchdog = (watchdog_hw->reason & WATCHDOG_REASON_TIMER_BITS) != 0U &&
		      watchdog_hw->scratch[MARK] != SOFT_REBOOT;
	fed_ms = watchdog_hw->scratch[FED_MS];
	watchdog_hw->scratch[MARK] = 0;
	return 0;
}

SYS_INIT(read_reset, PRE_KERNEL_1, 0);

static void feed(void *p1, void *p2, void *p3)
{
	const struct wdt_timeout_cfg cfg = {
		.window.max = TIMEOUT_MS,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int channel = wdt_install_timeout(wdt, &cfg);
	int ret = channel < 0 ? channel : wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);

	if (by_watchdog) {
		LOG_ERR("WATCHDOG RESET: hung after %u ms", fed_ms);
	}
	if (ret != 0) {
		LOG_ERR("watchdog: %d", ret);
		return;
	}
	for (;;) {
		(void)wdt_feed(wdt, channel);
		watchdog_hw->scratch[FED_MS] = k_uptime_get_32();
		k_msleep(FEED_MS);
	}
}

K_THREAD_DEFINE(gw_wdt_feed, 512, feed, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO - 1,
		0, 0);

static int cmd_reset(const struct shell *sh, size_t argc, char **argv)
{
	if (by_watchdog) {
		shell_print(sh, "last reset: WATCHDOG, hung after %u ms", fed_ms);
	} else {
		shell_print(sh, "last reset: not by the watchdog");
	}
	return 0;
}

SHELL_CMD_REGISTER(gw_reset, NULL, "whether the watchdog rebooted the gateway", cmd_reset);
