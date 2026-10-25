# Hello over the mcubus

Two equal MCUs, a (0x10) and b (0x11), on one I2C bus. Each one serves a service of its own, an echo, and calls the echo of the other. This is all it takes to add a service to the mcubus. How the bus works: [doc/mcubus.md](../../../doc/mcubus.md).

```c
#define ECHO_CMD 0x80   /* applications use 0x80 and up */

static int echo(const struct mcubus_req *req, uint8_t *rsp)
{
	rsp[0] = req->src;                          /* who sent it */
	memcpy(&rsp[1], req->data, req->len);       /* what it sent */
	return 1 + req->len;                        /* length of the payload */
}

MCUBUS_SERVICE_DEFINE(echo_service, ECHO_CMD, echo);
```

and to call it on the MCU at `peer`:

```c
mcubus_request(peer, ECHO_CMD, msg, len, rsp, 1 + len);
```

The service runs in the I2C target interrupt of the MCU that serves it, so it is short and only copies memory. It gets the address of the sender (`req->src`), and returns the length of its payload, or `-errno`, which `mcubus_request()` then returns on the other side.

Before that, each MCU pings the other until it answers (`mcubus_ping()`): the other one may still be booting.

## Build and run

Each MCU is its own native_sim process with its own image (`-DNODE=a|b`):

```
ws-zephyr/scripts/mcubus_run.sh ws-zephyr/samples/mcubus/hello a b
```

```
[a] [00:00:00.000,000] <inf> hello: mcu-a@10 at 0x10
[b] [00:00:00.000,000] <inf> hello: mcu-b@11 at 0x11
[b] [00:00:00.002,090] <inf> hello: 0x10 answers: protocol 1
[b] [00:00:00.011,180] <inf> hello: 0x10 echoed "hello from mcu-b@11", from 0x11
[a] [00:00:00.103,430] <inf> hello: 0x11 answers: protocol 1
[a] [00:00:00.112,180] <inf> hello: 0x11 echoed "hello from mcu-a@10", from 0x10
```

The second MCU to boot finds the first at once; the first pings until the second is there. Ctrl-C stops them, or `-- -stop_at=2` after the nodes stops them after two seconds.

With twister:

```
west twister -p native_sim -T ws-zephyr/samples/mcubus/hello
```

## Files

| File                        | What                                                         |
|-----------------------------|--------------------------------------------------------------|
| `src/main.c`                | The echo service, the ping, the echo request                 |
| `boards/native_sim.overlay` | The bus (`&i2c0`, bus 2) with both MCUs on it; `zephyr,mcubus-self` is the one this image is |
| `boards/native_sim.conf`    | Real time, and a tick fine enough for the emulated bus       |
| `CMakeLists.txt`            | `-DNODE=a\|b` picks the MCU                                  |
| `pytest/test_hello.py`      | The twister test: both echoes came back                      |
