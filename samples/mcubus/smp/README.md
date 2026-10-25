# MCUmgr over the mcubus

A board (a, 0x10) and a gateway (gw, 0x70) on one I2C bus. The board's shell is on a UART over the bus to the gateway, which bridges it to a PTY for the laptop (as in [`uart`](../uart)). MCUmgr's shell transport rides on that shell: an SMP client on the laptop talks to the board through the gateway and the bus, with no code on either MCU. How the bus and the UART work: [doc/mcubus.md](../../../doc/mcubus.md).

```
 laptop                    gateway (0x70)                              board a (0x10)
 smpmgr ── pty-a ── uart-bridge ── bus-uart-a ══ I2C ══ bus-uart ── shell ── MCUmgr
```

All of it is configuration: `board.conf` turns on MCUmgr with its shell transport and one group, `os` (echo, and the parameters SMP clients ask for first). The shell hands SMP frames on to MCUmgr, and the gateway carries them like any other byte.

## Build and run

```
ws-zephyr/scripts/mcubus_run.sh ws-zephyr/samples/mcubus/smp a gw
```

The gateway names its PTY, `[gw] pty-a connected to pseudotty: /dev/pts/7`. With `smpmgr` (`uv tool install smpmgr==0.19.1`), in a second terminal:

```
smpmgr --port /dev/pts/7 os echo "hello over the bus"
```

`tio` on the same PTY gives the shell; close it before `smpmgr`: they share the PTY.

With twister, which sends an echo and reads the parameters with `smpclient` (`ws-zephyr/requirements.txt`):

```
west twister -p native_sim -T ws-zephyr/samples/mcubus/smp
```

More groups are more lines in `board.conf`: [`applications/mcubus-system`](../../../applications/mcubus-system) adds files on littlefs (`fs`) and firmware images (`img`).

## Files

| File                          | What                                                     |
|-------------------------------|----------------------------------------------------------|
| `board.conf`, `board.overlay` | The board: shell on a `zephyr,mcubus-uart` to the gateway, MCUmgr on the shell |
| `gateway.overlay`             | The gateway: a `zephyr,mcubus-uart` to the board, bridged to the PTY |
| `boards/native_sim.overlay`   | The bus (`&i2c0`, bus 3) with both MCUs on it; the gateway's PTY |
| `boards/native_sim.conf`      | Real time, and a tick fine enough for the emulated bus   |
| `src/main.c`                  | Says which MCU runs                                      |
| `CMakeLists.txt`              | `-DNODE=a\|gw` picks the role and the MCU                |
| `pytest/test_smp.py`          | The twister test                                         |
