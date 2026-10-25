# mcubus: equal MCUs on one multi-master I2C bus

Several MCUs share one I2C bus, with no central node. Each one is a target at its own address and a controller when it sends. The mcubus carries requests between them; services give them a meaning.

```
 services    ping          settings          UART            (yours, cmd 0x80..)
                  \            |              /
 mcubus       request = cmd, src, flags, data;  response = status, payload
                               |
 link         one write + read transfer per message, retries
                               |
 I2C driver   any Zephyr I2C driver; i2c_mm_emul on native_sim
```

Each layer only knows the one below. Details are in the headers and bindings:

| Layer | Code | Interface |
|---|---|---|
| Link | `drivers/i2c/i2c_mm.c` | `<drivers/i2c/i2c_mm.h>`, `CONFIG_I2C_MM` |
| mcubus | `subsys/mcubus/mcubus.c` | `<mcubus/mcubus.h>`, `CONFIG_MCUBUS` |
| Ping (0x00) | `subsys/mcubus/ping.c` | `<mcubus/ping.h>`, `CONFIG_MCUBUS_PING` |
| Settings (0x01) | `subsys/mcubus/settings.c` | `<mcubus/settings.h>`, `CONFIG_MCUBUS_SETTINGS` |
| UART (0x10) | `drivers/serial/uart_mcubus.c` | `dts/bindings/mcubus/zephyr,mcubus-uart.yaml` |
| Bus on native_sim | `drivers/i2c/i2c_mm_emul.c` | `dts/bindings/i2c/zephyr,i2c-mm-emul.yaml`, `<drivers/i2c/i2c_mm_emul.h>` |

## Devicetree

The MCUs are children of the bus node, the same in every image; `/chosen zephyr,mcubus-self` says which one the image is. That enables `CONFIG_MCUBUS`.

```
&i2c0 {
	mcu_a: mcu-a@10 { compatible = "zephyr,mcubus-node"; reg = <0x10>; };
	mcu_b: mcu-b@11 { compatible = "zephyr,mcubus-node"; reg = <0x11>; };
};

/ {
	chosen {
		zephyr,mcubus-self = &mcu_a;
	};
};
```

## Samples and tests

| Where | What |
|---|---|
| `samples/mcubus/hello` | Ping, and a service of the application's own |
| `samples/mcubus/settings` | Get and set the other MCU's settings |
| `samples/mcubus/uart` | Shells of three boards through a gateway; the gateway on a Raspberry Pi Pico |
| `samples/mcubus/smp` | MCUmgr to a board through the gateway |
| `applications/mcubus-system` | All of it, as a product: settings in NVS, littlefs, MCUmgr (fs, img) |
| `tests/subsys/mcubus/api` | One MCU: services, faults |
| `tests/subsys/mcubus/stress` | Three MCUs sending at the same moments |

`scripts/mcubus_run.sh <sample> <nodes>` runs the MCUs of a sample on native_sim, one process each.

## On hardware

The I2C driver must be controller and target on one block, report a lost arbitration as `-EAGAIN`, recover a stuck bus before its START, report STOP to the target, and let the target answer within a byte time (or stretch SCL).

The RP2040 (`snps,designware-i2c`) is enough for a console gateway: it needs `CONFIG_I2C_MM_TARGET_REREGISTER`, reports lost arbitration as `-EIO`, and is no target during its own transfers. Good candidates for the product MCUs: STM32 I2C v2, NXP LPI2C.
