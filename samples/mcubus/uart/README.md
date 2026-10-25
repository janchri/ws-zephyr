# A UART over the mcubus: shells through a gateway

Three boards (a, b, c) and a console gateway (gw) on one I2C bus. Every board's shell is on a UART over the bus to the gateway (`zephyr,mcubus-uart`), and the gateway bridges each one to a serial port of its own: on the laptop, every board is a serial port. How the bus and the UART work: [doc/mcubus.md](../../../doc/mcubus.md).

```
 laptop                  gateway (0x70)                                board a (0x10)
 tio /dev/pts/7 ── pty-a ── uart-bridge ── bus-uart-a ══ I2C ══ bus-uart ── shell
                   pty-b ── uart-bridge ── bus-uart-b ══     ══ board b ...
                   pty-c ── uart-bridge ── bus-uart-c ══     ══ board c ...
```

No code does this: it is all devicetree. A board points `zephyr,shell-uart` at its UART to the gateway (`board.overlay`); the gateway has one UART per board, each in a `zephyr,uart-bridge` with a PTY (`gateway.overlay`). On hardware the PTYs are USB CDC-ACM ports; [below](#the-gateway-on-a-raspberry-pi-pico): the gateway on a Raspberry Pi Pico.

The boards have the `mcubus` shell commands:

```
mcu-a@10:~$ mcubus ping 0x11
0x11: protocol 1, messages up to 64 bytes
mcu-a@10:~$ mcubus stats
mcu-a@10 at 0x10: sent 21, served 6, retries 0
```

The ping goes over the bus twice: the command line and its output between the laptop and a, and the ping between a and b. The counts include the shell itself: every chunk of output that a board sends to the gateway is a request, and so is every chunk of typing it receives.

## Build and run

```
ws-zephyr/scripts/mcubus_run.sh ws-zephyr/samples/mcubus/uart a b c gw
```

builds the four MCUs (`west build -DNODE=a|b|c|gw`), runs them, and prints their logs; the gateway's log names the PTY of each board:

```
[gw] pty-a connected to pseudotty: /dev/pts/7
[gw] pty-b connected to pseudotty: /dev/pts/8
[gw] pty-c connected to pseudotty: /dev/pts/9
```

Open one with `tio`, `picocom` or `screen`, and press Enter for a prompt (a board that started before the gateway lost its first one). Ctrl-C stops the MCUs.

With twister, which runs `mcubus ping` and `help` in every board's shell:

```
west twister -p native_sim -T ws-zephyr/samples/mcubus/uart
```

## The gateway on a Raspberry Pi Pico

The gateway also runs on a Raspberry Pi Pico, on a real I2C bus: a USB device with one CDC-ACM port per board, "mcubus board a/b/c", instead of the PTYs. The boards on that bus need an I2C block that is controller and target at once ([doc/mcubus.md](../../../doc/mcubus.md#on-hardware)).

```
west build -b rpi_pico -d build-gw ws-zephyr/samples/mcubus/uart -- -DNODE=gw
```

Hold BOOTSEL while plugging the Pico in, and copy `build-gw/zephyr/zephyr.uf2` to the drive it shows. The bus is on I2C0: SDA GP4 (pin 6), SCL GP5 (pin 7), GND; it needs pull-ups (4.7 kΩ to 3.3 V). The Pico's log is on UART0 (GP0 TX, GP1 RX). On the laptop:

```
tio /dev/serial/by-id/usb-mcubus_mcubus_console_gateway-if00    # board a; b: if02, c: if04
```

Zephyr's driver for the Pico's I2C block (`i2c_dw`) does not switch back to target mode after a transfer; `CONFIG_I2C_MM_TARGET_REREGISTER` (on for it) registers the target again. It reports a lost arbitration as `-EIO`, which is not retried, and during its own transfers it is no target: characters can get lost when the gateway and a board send at the same moment.

## Files

| File                          | What                                                     |
|-------------------------------|----------------------------------------------------------|
| `board.overlay`, `board.conf` | A board: the shell on a `zephyr,mcubus-uart` to the gateway |
| `gateway.overlay`             | The gateway: per board a `zephyr,mcubus-uart`, bridged to a host port (`host_a`..`host_c`, from the board's overlay) |
| `mcubus_nodes.dtsi`           | The MCUs on the bus, included by every board's overlay   |
| `boards/native_sim.overlay`   | `&i2c0` as the multi-master port (bus 1) with the MCUs on it; the gateway's PTYs |
| `boards/native_sim.conf`      | Real time, and a tick fine enough for the emulated bus   |
| `boards/rpi_pico.overlay`, `.conf` | The gateway on a Pico: the bus on I2C0, a USB CDC-ACM port per board |
| `src/board.c`                 | A board: its prompt                                      |
| `src/gateway.c`               | The gateway: the USB device, if it has CDC-ACM ports     |
| `CMakeLists.txt`              | `-DNODE` picks the role and the MCU                      |
| `pytest/test_uart.py`         | The twister test                                         |

The gateway has the highest address: when it and a board start at the same moment, the board's request to another board wins.
