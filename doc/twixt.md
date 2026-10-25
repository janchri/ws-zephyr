# twixt: equal MCUs on one multi-master I2C bus

*Twixt* is old English for "between", as in "betwixt": twixt carries messages between MCUs. Its "twi" is also TWI, the two-wire interface, as I2C is often called.

Several MCUs share one I2C bus, with no central MCU. Each one is a target at its own address and a controller when it sends. Services give the requests a meaning; ids from `TWIXT_SERVICE_APP` (0x80) up are for applications.

```
 services    ping          settings          UART            (yours, 0x80..)
                  \            |              /
 twixt       request = service, src, flags, data;  response = status, data
                               |
 link         one write + read transfer per message, retries
                               |
 I2C driver   any Zephyr I2C driver; i2c_mm_native on native_sim
```

Each layer only knows the one below. Details are in the headers and bindings:

| Layer | Code | Interface |
|---|---|---|
| Link | `drivers/i2c/i2c_mm.c` | `<drivers/i2c/i2c_mm.h>`, `CONFIG_I2C_MM` |
| twixt | `subsys/twixt/twixt.c` | `<twixt/twixt.h>`, `CONFIG_TWIXT` |
| Ping (0x00) | `subsys/twixt/ping.c` | `<twixt/ping.h>`, `CONFIG_TWIXT_PING` |
| Settings (0x01) | `subsys/twixt/settings.c` | `<twixt/settings.h>`, `CONFIG_TWIXT_SETTINGS` |
| UART (0x10) | `drivers/serial/uart_twixt.c` | `dts/bindings/serial/twixt,uart.yaml` |
| Bus on native_sim | `drivers/i2c/i2c_mm_native.c` | `dts/bindings/i2c/twixt,native-i2c-mm.yaml`, `<drivers/i2c/i2c_mm_native.h>` |

## Devicetree

The MCUs are children of the bus node, the same in every image; `/chosen twixt,self` says which one the image is. That enables `CONFIG_TWIXT`.

```
&i2c0 {
	mcu_a: mcu-a@10 { compatible = "twixt,mcu"; reg = <0x10>; };
	mcu_b: mcu-b@11 { compatible = "twixt,mcu"; reg = <0x11>; };
};

/ {
	chosen {
		twixt,self = &mcu_a;
	};
};
```

## Samples and tests

| Where | What |
|---|---|
| `samples/twixt/hello` | Ping, and a service of the application's own |
| `samples/twixt/settings` | Get and set the other MCU's settings |
| `samples/twixt/uart` | Shells of three boards through a gateway; the gateway on a Raspberry Pi Pico |
| `samples/twixt/smp` | MCUmgr to a board through the gateway |
| `applications/twixt-system` | All of it, as a product: settings in NVS, littlefs, MCUmgr (fs, img) |
| `tests/subsys/twixt/api` | One MCU: services, faults |
| `tests/subsys/twixt/stress` | Three MCUs sending at the same moments |

`scripts/twixt_run.sh <sample> <mcus>` runs the MCUs of a sample on native_sim, one process each.

## On hardware

The I2C driver must be controller and target on one block, report a lost arbitration as `-EAGAIN`, recover a stuck bus before its START, report STOP to the target, and let the target answer within a byte time (or stretch SCL).

The RP2040 (`snps,designware-i2c`) is enough for a console gateway: it needs `CONFIG_I2C_MM_TARGET_REREGISTER`, reports lost arbitration as `-EIO`, and is no target during its own transfers. Good candidates for the product MCUs: STM32 I2C v2, NXP LPI2C.
