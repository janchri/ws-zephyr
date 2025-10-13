/*
 * Copyright (c) 2025 Christoph Jans <jans.christoph@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/w1.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/slist.h>
#include <zephyr/net/mqtt.h>

#include "w1_ds18b20.h"

LOG_MODULE_REGISTER(w1_ds18b20, CONFIG_LOG_DEFAULT_LEVEL);

#define CONFIG_APPLICATION_W1_DS18B20_INIT_PRIORITY 90
#define CONFIG_DS18B20_MAX_DEVICES 18
#define CONFIG_DS18B20_FAMILY_CODE 0x28

#define POLL_INTERVAL_SECONDS 60

struct ds18b20_config
{
    struct w1_rom w1_rom;
};

struct ds18b20_sensor{
    struct ds18b20_config cfg;
    struct ds18b20_value value;
};

const struct device *const w1_dev = DEVICE_DT_GET(DT_NODELABEL(w1));
const struct device *const ds18b20_dev = DEVICE_DT_GET_ANY(maxim_ds18b20);

ZBUS_CHAN_DEFINE(ds18b20_value_chan, struct ds18b20_value, NULL, NULL, ZBUS_OBSERVERS(mqtt_conn_sub), ZBUS_MSG_INIT(0));
ZBUS_CHAN_DECLARE(ds18b20_value_chan);

static struct ds18b20_sensor sensors[CONFIG_DS18B20_MAX_DEVICES];
static int sensor_count;

void w1_search_callback(struct w1_rom rom, void *user_data)
{
	LOG_INF("Device found; family: 0x%02x, serial: 0x%016llx", rom.family, w1_rom_to_uint64(&rom));
	if (rom.family == CONFIG_DS18B20_FAMILY_CODE)
    {
        sensors[sensor_count].value.rom = rom;
        sensor_count += 1;
    }
    else
    {
        LOG_WRN("Device code 0x%02X not supported", rom.family);
    }
}

static struct k_work_delayable w1_ds18b20_work;

static void poll_all_sensors(struct k_work *work)
{
	LOG_DBG("Start polling sensors");
    for (int i = 0; i < sensor_count; i++) {
	struct sensor_value attributes;
		struct ds18b20_sensor *sensor = &sensors[i];

		w1_rom_to_sensor_value(&sensor->value.rom, &attributes);
		int err = sensor_attr_set(ds18b20_dev, SENSOR_CHAN_AMBIENT_TEMP, SENSOR_ATTR_W1_ROM, &attributes);
		if (err != 0)
			LOG_DBG("sensor_attr_set: %d", err);

        sensor_sample_fetch(ds18b20_dev);
        sensor_channel_get(ds18b20_dev, SENSOR_CHAN_AMBIENT_TEMP, &sensor->value.temp);
		sensor->value.tempf = sensor_value_to_double(&sensor->value.temp);

        LOG_DBG("w1_sensor [%d,0x%016llx]: %.3f °C", i, w1_rom_to_uint64(&sensor->value.rom), (double)sensor->value.tempf);

		zbus_chan_pub(&ds18b20_value_chan, &sensor->value, K_MSEC(11));
    }
	LOG_DBG("End polling sensors - Start messaging");

	k_work_reschedule(&w1_ds18b20_work, K_SECONDS(POLL_INTERVAL_SECONDS));
}

static int w1_ds18b20_init(void)
{
	LOG_INF("w1_ds18b20_init Init");

	if (!device_is_ready(w1_dev)) {
		LOG_ERR("Device not ready");
		return 0;
	}

	if (!device_is_ready(ds18b20_dev))
    {
        LOG_ERR("One-Wire device is not ready");
        return 0;
    }

	int num_devices = w1_search_rom(w1_dev, w1_search_callback, NULL);

	LOG_INF("Number of devices found on bus: %d", num_devices);

	k_work_init_delayable(&w1_ds18b20_work, poll_all_sensors);
	k_work_schedule(&w1_ds18b20_work, K_SECONDS(POLL_INTERVAL_SECONDS));
	return 0;
}
SYS_INIT(w1_ds18b20_init, APPLICATION, CONFIG_APPLICATION_W1_DS18B20_INIT_PRIORITY);