# mcubus: equal MCUs on one multi-master I2C bus

Several MCUs share one I2C bus, with no server and no central node. Each MCU is an I2C target at its own address and, whenever it needs to, a controller. On that bus the mcubus carries requests between the MCUs; services give them a meaning:

- **settings**: get and set the Zephyr settings of any MCU by name,
- **UART**: a byte stream between two MCUs, for example a board's shell to a console gateway with USB.

```
 board a (0x10)                board b (0x11)                gateway (0x70)
 settings  shell               settings  shell               pty/USB per board
    |        |                    |        |                   |
 mcubus ── uart ──┐            mcubus ── uart ──┐            mcubus ── uart a, b, c
    |             |               |             |               |
 ═══╧═════════════╧═══════════════╧═════════════╧═══════════════╧═══  I2C (multi-master)
```

## Layers

```
 services    ping          settings          UART            (yours, cmd 0x80..)
             ping.c        settings.c        uart_mcubus.c
                  \            |              /
 mcubus       request = cmd, src, data;  response = status, payload
              dispatch by cmd, shell root "mcubus"            mcubus.c
                               |
 link         one write + read transfer per message, target interrupt,
 (i2c_mm)     arbitration retries, driver quirks              i2c_mm.c
                               |
 I2C driver   Zephyr I2C API: i2c_mm_emul (native_sim), i2c_dw, ...
```

Every layer only knows the one below. The link moves bytes and knows nothing about cmds; mcubus knows the frame and its services only as entries of an iterable section; a service knows its own cmd, its data, and its shell commands (added under `mcubus` with `SHELL_SUBCMD_ADD`).

## Pieces

| Where (ws-zephyr/)                          | What                                                        |
|---------------------------------------------|-------------------------------------------------------------|
| `drivers/i2c/i2c_mm.c`, `include/drivers/i2c/i2c_mm.h` | `CONFIG_I2C_MM`: the link; serves requests as a target, sends them as a controller, retries |
| `subsys/mcubus/mcubus.c`, `include/mcubus/mcubus.h` | `CONFIG_MCUBUS`: the frame, the dispatch to the services, `mcubus stats` |
| `subsys/mcubus/ping.c`, `include/mcubus/ping.h` | `CONFIG_MCUBUS_PING` (default y): `mcubus_ping()`, `mcubus ping` |
| `subsys/mcubus/settings.c`, `include/mcubus/settings.h` | `CONFIG_MCUBUS_SETTINGS`: `mcubus_settings_get()` / `_set()`, `mcubus get` / `set` |
| `drivers/serial/uart_mcubus.c`              | `CONFIG_UART_MCUBUS`, compatible `zephyr,mcubus-uart`        |
| `drivers/i2c/i2c_mm_emul.c`, `i2c_mm_host.c` | `CONFIG_I2C_MM_EMUL`, compatible `zephyr,i2c-mm-emul`: the bus on native_sim |
| `dts/bindings/mcubus/`                      | `zephyr,mcubus-node` (an MCU on the bus), `zephyr,mcubus-uart` |
| `samples/mcubus/hello`                      | Two MCUs: ping, and a service of the application's own (echo) |
| `samples/mcubus/settings`                   | Two MCUs get and set each other's settings by name          |
| `samples/mcubus/uart`                       | The shells of three boards, through a gateway, on `/dev/pts`; on hardware: the gateway on a Raspberry Pi Pico |
| `samples/mcubus/smp`                        | MCUmgr (SMP) to a board, through the gateway: echo          |
| `applications/mcubus-system`                | All of it as a product would have it: boards with settings in NVS, littlefs, MCUmgr (fs, img), and the gateway |
| `scripts/mcubus_run.sh`, `scripts/pylib/mcubus_bench.py` | Run the MCUs of a sample on native_sim; the twister test bench |
| `tests/subsys/mcubus/api`                   | ztest, one MCU: settings, ping, and the bus under faults    |
| `tests/subsys/mcubus/stress`                | Three MCUs send random requests at the same moments: arbitration, and every request served once |

The link, and so everything above it, only uses the Zephyr I2C API (`i2c_transfer()`, `i2c_target_register()`): on hardware they run on the I2C driver of the MCU.

## Devicetree

The MCUs are children of the bus node, at their addresses: the address plan of the bus, the same for every MCU. Each image says which of them it is.

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

With `zephyr,mcubus-self`, `CONFIG_MCUBUS` is on by default (it needs `CONFIG_I2C`). From boot on, the MCU serves requests at its address.

## Requests

A request is one I2C transfer, so no other master can come in between:

```
write:  cmd, src, data
repeated START
read:   status, payload      (status negative is -errno, then no payload)
```

`src` is the address of the sender, filled in by `mcubus_request()`: an I2C target cannot see who the controller is, and a service that answers per peer (the UART) needs to know. A request shorter than cmd and src gets `-EINVAL`.

The cmd picks the service (`MCUBUS_SERVICE_DEFINE`); a cmd no service has gets `-ENOTSUP`. A service writes only its payload, or returns -errno; mcubus puts the status in front, and `mcubus_request()` returns a negative status as its result, so callers get the payload alone. The target runs the service in its I2C interrupt when the read begins, so a service is short and only copies memory. A request of an MCU to itself does not go over the bus: the service runs in the calling thread.

| cmd         | Service   | Data                       | Payload of the response |
|-------------|-----------|----------------------------|-------------------------|
| `0x00`      | ping      | (nothing)                  | protocol version, `MCUBUS_MSG_MAX` |
| `0x01`      | settings  | `GET` (0), key, NUL        | len, value              |
|             |           | `SET` (1), key, NUL, value | (nothing)               |
| `0x10`      | UART      | bytes                      | how many were taken     |
| `0x80..ff`  | applications |                         |                         |

Each service defines its cmd in its own header (the UART in its driver); `mcubus.h` keeps the list above as a comment. Two services with one cmd stop mcubus at boot (`-EEXIST`, logged).

When controllers start at the same moment, the lowest frame wins the arbitration: the address byte first, then the cmd. A lower address and a lower cmd have the higher priority: settings go before UART bytes. The losers get `-EAGAIN`; the link backs off and retries for up to `CONFIG_I2C_MM_RETRY_MS` (500 ms).

Every MCU on a bus needs the same protocol (`MCUBUS_PROTOCOL_VERSION`) and the same `CONFIG_I2C_MM_MSG_MAX` (`MCUBUS_MSG_MAX`). `mcubus_ping()` asks another MCU for both, and returns `-EPROTO` if they differ from its own.

### Shell

With `CONFIG_MCUBUS_SHELL` (on with the shell); each service adds its own commands:

```
mcubus stats                    requests sent, served, retried (the link's)
mcubus ping <addr>              protocol and limits of the MCU at <addr> (MCUBUS_PING)
mcubus get <addr> <key>         a setting of the MCU at <addr> (MCUBUS_SETTINGS)
mcubus set <addr> <key> <text>
```

### Faults

- **A request runs at most once.** The link's target forgets a request once it has run it, and at every STOP. So a read without a request (a bus scan, a controller that lost its place) gets an empty request, which mcubus answers with `-EINVAL`, and a request whose read never came (its controller gave up) does not run, not even later.
- **Its controller may not learn that it ran.** If the read of the response fails, the controller gets an error, and the request may have run or not. A settings SET can be sent again; UART bytes sent again arrive twice.
- **A request that does not fit** `MCUBUS_MSG_MAX` is NACKed by the link: `-EIO`.
- **A stuck bus is the I2C driver's to recover.** A target that was reset in the middle of a read can hold SDA low. Only the driver can tell that from another master's transfer (SDA low, but no START on the bus), so it clocks the bus free (nine clocks, a STOP) before its own START. mcubus does not guess it from an error code: recovering while another master owns the bus would break that master's transfer.
- **No checksum.** An ACK only says that a byte arrived. A request with a bit error is run as it arrived.

### Settings

`settings_runtime_get()` / `settings_runtime_set()` look up the settings handler and call it, in the I2C interrupt. They take no lock in the settings subsystem, so the handlers keep their values whole themselves: static (`SETTINGS_STATIC_HANDLER_DEFINE`), only copying, under a `k_spinlock`, since they run in threads too. A handler refuses a read-only setting with `-EACCES`. After a SET, a work item calls `settings_save()`: flash is too slow for an interrupt.

### UART

`zephyr,mcubus-uart` is a UART device (interrupt-driven API) whose other end is the MCU `peer`. Sending is pushing: a thread sends what was written in requests of up to `CONFIG_UART_MCUBUS_CHUNK` (32) bytes; the peer answers how many it took, and the rest is sent again, so a full receive buffer holds the sender back. If nobody answers at `peer`, the bytes are dropped, as on a UART without a cable. Received bytes go, in the interrupt, to the UART whose `peer` sent them (`src`).

The stock shell (`zephyr,shell-uart`), console and `zephyr,uart-bridge` use it as they are. A board points its shell at a UART to the gateway; the gateway has one UART per board, each bridged to a serial port of its own (PTY on native_sim, USB CDC-ACM on hardware). MCUmgr's shell transport uses the same line for SMP: files and firmware images from the laptop (`samples/mcubus/smp`, `applications/mcubus-system`).

## The bus on native_sim

`zephyr,i2c-mm-emul` is the I2C block of one MCU, controller and target. Every native_sim process is one MCU; the processes with the same `bus-id` share one bus.

- **Wires**: a POSIX shared memory segment, `/dev/shm/zephyr_i2c_mm_<bus-id>` (`i2c_mm_host.c`, Linux side): who owns the bus, the arbitration, which process is a target at which address, a mailbox per target. Slots of dead processes are reclaimed; the last process removes the segment.
- **Ownership**: a controller that finds the bus busy waits for the STOP, up to `bus-timeout-us`, then gets `-EBUSY`.
- **Arbitration**: controllers whose START falls in the same `start-window-us` compare address byte and first data bytes; the lower wins, the others get `-EAGAIN`. Separate processes cannot be as simultaneous as a real bus, so the window is wide, and every transfer pays it as START latency. The winner is decided on the wall clock all processes share, not on simulated time.
- **Interrupts**: address match, every byte and STOP raise the port's native_sim interrupt line (`irq-line`), so the target callbacks run in a real ISR; the controller waits for it, like SCL stretching. A watcher thread plays the target's bus hardware.
- **Timing**: a byte is 9 bit times. Use `CONFIG_SYS_CLOCK_TICKS_PER_SEC=100000` (a 10 ms tick rounds every byte up to 10 ms) and run in real time (`CONFIG_NATIVE_SIM_SLOWDOWN_TO_REAL_TIME`): the processes find each other in wall-clock time.
- **Recovery**: a controller that finds the bus stuck frees it before its START, and counts it (`recoveries` in `i2c_mm_emul_get_stats()`); `i2c_recover_bus()` does it on request.
- **Loopback** (devicetree property `loopback`): unlike most hardware, a controller can address the target of its own port. Only the ztest sets it, to send raw transfers to its own target.
- **Test bench**, nothing a real MCU has: `i2c_mm_emul_wait_nodes()` (wait for the others to start), `i2c_mm_emul_wait_round()` (the same wall-clock moments on every MCU, so that they collide), `i2c_mm_emul_stick_bus()` (a target holds SDA low), `i2c_mm_emul_get_stats()`.

Not modelled: 10-bit addresses, target buffer mode, more than one port per process, two masters sending identical frames.

A process left over from an earlier run joins the bus of the next one. The twister tests of the samples refuse to start while a process has the bus mapped.

## On hardware

What the emulation settles, an I2C driver has to provide too:

1. An arbitration loss aborts the transfer with its own error (`-EAGAIN`); the link retries. A busy bus ends with `-EBUSY`, retried too.
2. The driver detects a stuck bus (SDA held low with no START) and clocks it free before its START, without being asked.
3. The target answers within a byte time, unless the hardware stretches SCL. The services, and so the settings handlers, stay short and never block.
4. A request and its response are one transfer with a repeated START.
5. The target reports STOP (`stop` callback), so that a request without its read is dropped.

### Drivers

| MCU, driver | Controller and target | What mcubus does | What is still missing |
|---|---|---|---|
| native_sim, `zephyr,i2c-mm-emul` | one block | | |
| RP2040, `snps,designware-i2c` | one block, either controller or target | `CONFIG_I2C_MM_TARGET_REREGISTER`: the driver switches to controller mode for a transfer and stays there, so the link registers the target again after each one | An arbitration loss is `-EIO`, not `-EAGAIN`: not retried (1). During its own transfer the block is no target: a request to it then is NACKed. |

The Pico is good enough for a console gateway (`samples/mcubus/uart`), whose requests seldom meet the boards'; a product's MCUs need drivers that keep all five.
