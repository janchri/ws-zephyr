#ifndef WS_DS18B20_H
#define WS_DS18B20_H

#include <zephyr/drivers/w1.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include <zephyr/zbus/zbus.h>

struct ds18b20_value{
    struct w1_rom rom;
    struct sensor_value temp;
	float tempf;
};
ZBUS_CHAN_DECLARE(ds18b20_value_chan);

#endif /* WS_DS18B20_H */