#ifndef ONEWIRE_H
#define ONEWIRE_H

#include <zephyr/drivers/w1.h>
#include <zephyr/drivers/sensor/w1_sensor.h>
#include <zephyr/zbus/zbus.h>

struct ds18b20_value{
    struct w1_rom rom;
    struct sensor_value temp;
	float tempf;
};

/*
0x2877356e542208a7
0x28c7bf8c54220898
0x283b4142602206e0
0x282bfc676122071c
0x280b8c7954220840
0x28f3c79854220992
0x2813d454542209a2
0x28c31469542208cf
0x2843195b542209d8
0x28adf37654220836
0x2895133d6122077e
0x28a6056a54220980
0x281aa2935422097a
0x28e2fb416122073d
0x2884ce57542209df
0x28c8723c542209bd
0x2810b7715422080b
0x2820a686542209fe
*/

#endif /* ONEWIRE_H */