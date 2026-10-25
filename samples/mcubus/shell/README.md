# Shells over the mcubus (native_sim)

Three boards (a, b, c) and a console gateway (gw) on one I2C bus. Every board's shell is on a UART over the bus to the gateway, and the gateway bridges each one to a PTY of its own: on the laptop, every board is a serial port. How the bus and the UART work: [doc/mcubus.md](../../../doc/mcubus.md).

```
 laptop                  gateway (0x70)                                board a (0x10)
 tio /dev/pts/7 ── pty-a ── uart-bridge ── bus-uart-a ══ I2C ══ bus-uart ── shell
                   pty-b ── uart-bridge ── bus-uart-b ══     ══ board b ...
                   pty-c ── uart-bridge ── bus-uart-c ══     ══ board c ...
```

On hardware the gateway is a small board with USB, and the PTYs are USB CDC-ACM ports; the boards stay as they are. [Below](#the-gateway-on-a-raspberry-pi-pico): the gateway on a Raspberry Pi Pico.

The boards also serve a setting, `mcu/name`, and have the `mcubus` shell commands:

```
mcu-a@10:~$ mcubus ping 0x11
0x11: protocol 1, messages up to 64 bytes
mcu-a@10:~$ mcubus get 0x11 mcu/name
0x11 mcu/name = "mcu-b@11"
mcu-a@10:~$ mcubus stats
mcu-a@10 at 0x10: sent 21, served 6, retries 0
```

The counts include the shell itself: every chunk of output that a board sends to the gateway is a request, and so is every chunk of typing it receives.

That goes over the bus twice: the command line and its output between the laptop and a, and the request between a and b.

Each board's flash has the partitions of the product: `settings` (32 KiB, NVS, the backend of the settings) and `storage` (256 KiB, littlefs, mounted at `/lfs`). The `fs` shell commands work on it:

```
mcu-a@10:~$ fs write /lfs/hello -o 0 68 69 0a
mcu-a@10:~$ fs cat /lfs/hello
hi
mcu-a@10:~$ fs ls /lfs
       3 hello
mcu-a@10:~$ fs statvfs /lfs
bsize 16, frsize 4096, blocks 64, bfree 62
```

The first boot formats the storage partition. littlefs is the west project `littlefs`, in the `name-allowlist` of `ws-zephyr/west.yml`.

### Files and firmware from the laptop: MCUmgr

The boards speak MCUmgr's SMP protocol on the same PTY as their shell (`CONFIG_MCUMGR_TRANSPORT_SHELL`): the shell passes SMP frames on to MCUmgr, and the gateway carries them like any other byte. Two groups: **fs** (upload and download files on `/lfs`) and **img** (upload a firmware image into slot 1). Any SMP client on the laptop works; `smpmgr` is the command line tool on top of `smpclient`, which the twister test uses (`uv tool install smpmgr==0.19.1`). With the PTY of board a that `scripts/run.sh` prints:

```
smpmgr --port /dev/pts/7 file upload notes.txt /lfs/notes.txt
smpmgr --port /dev/pts/7 file download /lfs/notes.txt back.txt
smpmgr --port /dev/pts/7 image upload zephyr.signed.bin
smpmgr --port /dev/pts/7 image state-read
```

Close `tio` on that PTY first: the shell and SMP share it. Every command logs a warning that reading the MCUmgr parameters failed: that request belongs to the os group, which the boards leave out; the client goes on with its defaults.

An image must be in MCUboot's format (`west sign`, or `imgtool create`). On native_sim there is no MCUboot: the image lands in slot 1 and `image state-read` shows it, but nothing boots it; on hardware, MCUboot swaps it in at the next reset. SMP frames carry a CRC16 of their own, and MCUboot checks the image's hash, so a frame that the bus garbles is rejected, not written. On native_sim a transfer runs at about 1.5 KB/s. The image group needs the west projects `mcuboot` (its bootutil library) and `zcbor`.

## Build and run

```
ws-zephyr/samples/mcubus/shell/scripts/run.sh
```

builds the four MCUs (`west build -DNODE=a|b|c|gw`), runs them, and prints the PTY of each board's shell:

```
board a: /dev/pts/7
board b: /dev/pts/8
board c: /dev/pts/9
```

Open one with `tio`, `picocom` or `screen`, and press Enter for a prompt (a board that started before the gateway lost its first one). Ctrl-C stops the MCUs. Every board has its own flash file, `build-mcubus-shell.<a,b,c>.flash.bin`: its settings and its files are still there on the next run. Their logs stay on stdout, in `build-mcubus-shell.<a,b,c,gw>.log`: the shell is not their log backend (`CONFIG_SHELL_LOG_BACKEND=n`), so that logs do not fill the bus.

### With twister

```
west twister -p native_sim -T ws-zephyr/samples/mcubus/shell
```

Twister builds the four MCUs and starts the gateway; `pytest/test_shell.py` starts the boards (`required_applications`), opens the PTY of each, and runs in its shell: `mcubus ping` and `mcubus get` of the next board, `mcubus set` of a read-only setting (-13, `-EACCES`), a file written, listed and read back on `/lfs`, and `help`, whose output is many chunks in a row. `test_smp` then uses MCUmgr on board a's PTY: a file to `/lfs` and back, an image into slot 1. Every board runs in its build folder, so its flash is the `flash.bin` there. The Python packages are in `ws-zephyr/requirements.txt`.

## The gateway on a Raspberry Pi Pico

The gateway also runs on a Raspberry Pi Pico, on a real I2C bus: a USB device with one CDC-ACM port per board, "mcubus board a/b/c", instead of the PTYs. The boards on that bus need an I2C block that is controller and target at once ([doc/mcubus.md](../../../doc/mcubus.md#on-hardware)).

```
west build -b rpi_pico -d build-gw ws-zephyr/samples/mcubus/shell -- -DNODE=gw
```

Hold BOOTSEL while plugging the Pico in, and copy `build-gw/zephyr/zephyr.uf2` to the drive it shows. The bus is on I2C0: SDA GP4 (pin 6), SCL GP5 (pin 7), GND; it needs pull-ups (4.7 kΩ to 3.3 V). The Pico's log is on UART0 (GP0 TX, GP1 RX). On the laptop:

```
tio /dev/serial/by-id/usb-mcubus_mcubus_console_gateway-if00    # board a; b: if02, c: if04
```

Zephyr's driver for the Pico's I2C block (`i2c_dw`) does not switch back to target mode after a transfer; `CONFIG_MCUBUS_TARGET_REREGISTER` (on for it) registers the target again. It reports a lost arbitration as `-EIO`, which is not retried, and during its own transfers it is no target: characters can get lost when the gateway and a board send at the same moment.

## Files

| File                          | What                                                     |
|-------------------------------|----------------------------------------------------------|
| `boards/native_sim.overlay`   | `&i2c0` as the multi-master port (bus 1) with the MCUs on it, `zephyr,mcubus-self = &MCUBUS_SELF_LABEL`; the gateway's PTYs; a board's partitions, the image slots as mapped partitions |
| `boards/native_sim.conf`      | Real time, a tick fine enough for the emulated bus; the image runs from slot 0 |
| `boards/rpi_pico.overlay`, `.conf` | The gateway on a Pico: the bus on I2C0, a USB CDC-ACM port per board |
| `mcubus_nodes.dtsi`           | The MCUs on the bus, included by every board's overlay   |
| `board.overlay`, `board.conf` | A board: shell on a `zephyr,mcubus-uart` to the gateway; NVS on the settings partition, littlefs on the storage partition at `/lfs`; MCUmgr (fs, img) on the shell |
| `gateway.overlay`             | The gateway: per board a `zephyr,mcubus-uart`, bridged to a host port (`host_a`..`host_c`, from the board's overlay) |
| `src/board.c`                 | A board: its setting `mcu/name`, its prompt              |
| `src/gateway.c`               | The gateway: the USB device, if it has CDC-ACM ports     |
| `CMakeLists.txt`              | `-DNODE` picks the role and `MCUBUS_SELF_LABEL=mcu_<NODE>` |
| `scripts/run.sh`              | Builds and runs the four MCUs                            |
| `pytest/test_shell.py`        | The twister test                                         |

The gateway has the highest address: when it and a board start at the same moment, the board's request to another board wins.
