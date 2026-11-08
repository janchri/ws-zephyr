/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <zephyr/app_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell_uart.h>
#include <twixt/twixt.h>

LOG_MODULE_REGISTER(twixt_system, LOG_LEVEL_INF);

#ifdef CONFIG_TWIXT_USB_GATEWAY
int twixt_usb_init(void);
#endif

int main(void)
{
	static char prompt[32];
	int ret = settings_subsys_init();

	if (ret == 0) {
		ret = settings_load();
	}
	if (ret) {
		LOG_ERR("settings: %d", ret);
	}
#ifdef CONFIG_TWIXT_USB_GATEWAY
	ret = twixt_usb_init();
	if (ret) {
		LOG_ERR("USB: %d", ret);
	}
#endif
	if (IS_ENABLED(CONFIG_SHELL)) {
		snprintf(prompt, sizeof(prompt), "%s:~$ ", twixt_self_name());
		shell_prompt_change(shell_backend_uart_get_ptr(), prompt);
	}
	LOG_INF("%s at 0x%02x: %s, v%s", twixt_self_name(), twixt_self_addr(),
		IS_ENABLED(CONFIG_TWIXT_GATEWAY) ? "gateway" : "node", APP_VERSION_STRING);
	return 0;
}
