# Settings over the mcubus (native_sim)

Three equal MCUs on one I2C bus get and set each other's settings by name, all at the same moment, so that their requests collide and arbitrate. Each MCU is its own native_sim process, the same code with its own place in the devicetree. How the bus and the requests work: [doc/mcubus.md](../../../doc/mcubus.md).

## The settings

Every MCU has the same settings, in the subtree `mcu`:

| Setting        | Access | Value                                            |
|----------------|--------|--------------------------------------------------|
| `mcu/name`     | r      | the devicetree node of the MCU, e.g. `mcu-a@10`  |
| `mcu/version`  | r      | `APP_VERSION` (`1.0.0`)                          |
| `mcu/counter`  | r      | the rounds of this MCU so far, 32 bit            |
| `mcu/dump0..2` | rw     | any bytes, up to 16; kept in flash               |

## What it checks

First every MCU pings the others: the same protocol version and limits. Then, in each of 20 rounds, every MCU sends a random request to a random MCU, maybe itself, all at the same moment, and a little later another one. It checks the answer:

- name and version are what the devicetree and the build say,
- a counter never goes back, and setting it fails with `-EACCES`,
- a dump is whole: whoever set it put a CRC into its last byte (or it was never set, `-ENOENT`),
- `mcu/nothing` does not exist (`-ENOENT`).

Before the rounds it checks that the bus NACKs an address nobody has (0x55). At the end, every request that was sent must have been served by some MCU. Requests of an MCU to itself do not go over the bus and are not counted.

## Build and run

```
ws-zephyr/samples/mcubus/settings/scripts/run.sh
```

builds the three MCUs (`west build -DNODE=a|b|c`), runs them, and prints the merged log, time stamp first, then the MCU. The run takes about three seconds and ends like this (the counts vary):

```
[a] sent 25 requests, served 32 requests; 0 bad; 16 retries; lost arbitration 16, ...
[b] sent 31 requests, served 28 requests; 0 bad; 22 retries; lost arbitration 22, ...
[c] sent 29 requests, served 25 requests; 0 bad; 14 retries; lost arbitration 14, ...
85 requests sent, 85 served
all MCUs verified the settings
```

- `SKIP_BUILD=1 scripts/run.sh` runs what was built before. The dumps of the last run are still in flash (`<build-dir>.<a,b,c>.flash.bin`); the MCUs log their sizes at boot.
- `SEED=7 scripts/run.sh` changes the random choices. Every MCU needs its own `--seed`; with the same seeds the choices repeat, the retries do not (they depend on the host scheduler).

By hand, every MCU needs its own flash file, and the three must start within a second of each other (round 0 is one second after the first came up):

```
west build -b native_sim -d build/a ws-zephyr/samples/mcubus/settings -- -DNODE=a
...
build/a/zephyr/zephyr.exe --seed=1 --flash=a.bin &
build/b/zephyr/zephyr.exe --seed=2 --flash=b.bin &
build/c/zephyr/zephyr.exe --seed=3 --flash=c.bin
```

### With twister

```
west twister -p native_sim -T ws-zephyr/samples/mcubus/settings
```

does the same, with the same checks. `sample.yaml` has a build per MCU (`extra_args: NODE=a|b|c`); twister starts a, and `pytest/test_settings.py` starts b and c from their builds (`required_applications`). Their logs are `peer.log` in their build folders.

## Files

| File                        | What                                                          |
|-----------------------------|---------------------------------------------------------------|
| `src/main.c`                | One MCU: its settings handler, the rounds and their checks    |
| `boards/native_sim.overlay` | `&i2c0` as the multi-master port (bus 0), the MCUs on it, and `zephyr,mcubus-self = &MCUBUS_SELF_LABEL` |
| `CMakeLists.txt`            | `-DNODE=<a\|b\|c>` becomes `MCUBUS_SELF_LABEL=mcu_<NODE>`     |
| `scripts/run.sh`            | Builds and runs the three MCUs                                |
| `pytest/test_settings.py`   | The twister test                                              |

A fourth MCU is one more node in the overlay, and `d` in `CMakeLists.txt` and `scripts/run.sh`.
