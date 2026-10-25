# Settings over the mcubus

Two equal MCUs, a (0x10) and b (0x11), on one I2C bus. Each one serves its Zephyr settings to the other, by name, and gets and sets the other's (`CONFIG_MCUBUS_SETTINGS`, `<mcubus/settings.h>`). How the bus works: [doc/mcubus.md](../../../doc/mcubus.md).

| Setting        | Access     | Value                                       |
|----------------|------------|---------------------------------------------|
| `mcu/name`     | read-only  | the devicetree node of the MCU, `mcu-a@10`  |
| `mcu/greeting` | read/write | any text, kept in flash                     |

Each MCU gets the other's name, sets the other's greeting, reads it back, and tries to set the other's name:

```c
mcubus_settings_get(peer, "mcu/name", val, sizeof(val));     /* length of the value, or -errno */
mcubus_settings_set(peer, "mcu/greeting", hi, len);          /* 0, or -errno */
mcubus_settings_set(peer, "mcu/name", "x", 1);               /* -EACCES: read-only */
```

The settings are plain Zephyr settings (`SETTINGS_STATIC_HANDLER_DEFINE`). The other MCU's requests run in the I2C target interrupt, and so do the handlers: they only copy memory, under a spinlock. After a set, mcubus stores the settings from a work item (`settings_save()`), so the greeting is still there after a reset.

## Build and run

Each MCU is its own native_sim process with its own image (`-DNODE=a|b`) and its own flash file:

```
ws-zephyr/scripts/mcubus_run.sh ws-zephyr/samples/mcubus/settings a b
```

```
[a] [00:00:00.000,000] <inf> settings_sample: mcu-a@10 at 0x10, greeting from flash: ""
[b] [00:00:00.000,000] <inf> settings_sample: mcu-b@11 at 0x11, greeting from flash: ""
[b] [00:00:00.013,250] <inf> settings_sample: 0x10 is mcu-a@10
[b] [00:00:00.029,730] <inf> settings_sample: 0x10 mcu/greeting = "hi from mcu-b@11"
[b] [00:00:00.034,780] <inf> settings_sample: 0x10 mcu/name is read-only: -13
[a] [00:00:00.115,210] <inf> settings_sample: 0x11 is mcu-b@11
[a] [00:00:00.131,880] <inf> settings_sample: 0x11 mcu/greeting = "hi from mcu-a@10"
[a] [00:00:00.137,010] <inf> settings_sample: 0x11 mcu/name is read-only: -13
```

Ctrl-C stops them. Run it again (`SKIP_BUILD=1`): now each one boots with the greeting the other gave it, from `build-mcubus-settings/<a|b>.flash.bin`.

With twister, which also restarts b to check that its greeting is still in flash:

```
west twister -p native_sim -T ws-zephyr/samples/mcubus/settings
```

## Files

| File                        | What                                                         |
|-----------------------------|--------------------------------------------------------------|
| `src/main.c`                | The settings handler of `mcu`, the requests to the other MCU |
| `prj.conf`                  | Settings in NVS, `CONFIG_MCUBUS_SETTINGS`                    |
| `boards/native_sim.overlay` | The bus (`&i2c0`, bus 0) with both MCUs on it; `zephyr,mcubus-self` is the one this image is |
| `boards/native_sim.conf`    | Real time, and a tick fine enough for the emulated bus       |
| `CMakeLists.txt`            | `-DNODE=a\|b` picks the MCU                                  |
| `pytest/test_settings.py`   | The twister test                                             |
