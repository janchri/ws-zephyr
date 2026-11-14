/*
 * Copyright (c) 2026 Christoph Jans
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The settings of this MCU, for "twixt get/set" on the others: mcu/name (read
 * only), mcu/label (saved where the MCU has a settings backend) and mcu/dump
 * (RAM only). They are served in the I2C target interrupt too. Shared by nodes and gateways.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <twixt/twixt.h>

struct value {
	uint8_t data[CONFIG_TWIXT_SETTINGS_VALUE_MAX];
	size_t len;
};

static struct k_spinlock lock;
static struct value label;
static struct value dump;

static struct value *find(const char *key)
{
	if (settings_name_steq(key, "label", NULL)) {
		return &label;
	}
	if (settings_name_steq(key, "dump", NULL)) {
		return &dump;
	}
	return NULL;
}

static int mcu_get(const char *key, char *val, int val_len_max)
{
	struct value *v = find(key);
	int ret = -ENOMEM;

	if (settings_name_steq(key, "name", NULL)) {
		ret = strlen(twixt_self_name());
		if (ret > val_len_max) {
			return -ENOMEM;
		}
		memcpy(val, twixt_self_name(), ret);
		return ret;
	}
	if (v == NULL) {
		return -ENOENT;
	}
	K_SPINLOCK(&lock) {
		if (v->len <= val_len_max) {
			memcpy(val, v->data, v->len);
			ret = v->len;
		}
	}
	return ret;
}

static int mcu_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct value *v = find(key);
	uint8_t buf[CONFIG_TWIXT_SETTINGS_VALUE_MAX];
	ssize_t n;

	if (settings_name_steq(key, "name", NULL)) {
		return -EACCES;
	}
	if (v == NULL) {
		return -ENOENT;
	}
	if (len > sizeof(buf)) {
		return -ENOMEM;
	}
	n = read_cb(cb_arg, buf, len);
	if (n < 0) {
		return n;
	}
	K_SPINLOCK(&lock) {
		memcpy(v->data, buf, n);
		v->len = n;
	}
	return 0;
}

static int mcu_export(int (*cb)(const char *name, const void *value, size_t val_len))
{
	struct value copy;

	K_SPINLOCK(&lock) {
		copy = label;
	}
	return cb("mcu/label", copy.data, copy.len);
}

SETTINGS_STATIC_HANDLER_DEFINE(mcu, "mcu", mcu_get, mcu_set, NULL, mcu_export);
