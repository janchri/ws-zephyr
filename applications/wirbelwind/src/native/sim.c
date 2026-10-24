/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * native_sim stand-ins for the controls of the box, driven from the shell:
 *   ww button [ms]     the button, via the GPIO emulator
 *   ww pot <0..100>    the volume pot, via the ADC emulator
 * Both go through gpio-keys / analog-axis like on the real board.
 */

#include <stdlib.h>

#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/shell/shell.h>

/* Button and volume pot */

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(button), gpios);

#define POT_AXIS DT_CHILD(DT_NODELABEL(volume_pot), axis)
#define POT_ADC  DEVICE_DT_GET(DT_IO_CHANNELS_CTLR(POT_AXIS))
#define POT_CHAN DT_IO_CHANNELS_INPUT(POT_AXIS)
#define POT_MV   DT_PROP(DT_IO_CHANNELS_CTLR(POT_AXIS), ref_internal_mv)

static bool button_held;

static void release_button(struct k_work *work)
{
	gpio_emul_input_set(button.port, button.pin, 0);
	button_held = false;
}

static K_WORK_DELAYABLE_DEFINE(release_work, release_button);

static int cmd_button(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t hold_ms = argc > 1 ? strtoul(argv[1], NULL, 0) : 100;

	if (button_held) {
		shell_error(sh, "The button is still held");
		return -EBUSY;
	}

	button_held = true;
	gpio_emul_input_set(button.port, button.pin, 1);
	k_work_schedule(&release_work, K_MSEC(hold_ms));

	return 0;
}

static int cmd_pot(const struct shell *sh, size_t argc, char **argv)
{
	int percent = CLAMP(strtol(argv[1], NULL, 0), 0, 100);

	return adc_emul_const_value_set(POT_ADC, POT_CHAN, POT_MV * percent / 100);
}

SHELL_SUBCMD_ADD((ww), button, NULL,
		 "Press the button: button [hold ms, default 100]; "
		 "<1 s next track, >=1 s restart playlist, >=10 s reset all",
		 cmd_button, 1, 1);
SHELL_SUBCMD_ADD((ww), pot, NULL, "Turn the volume pot: pot <0..100>", cmd_pot, 2, 0);
