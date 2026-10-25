# Hello over the mcubus

Two MCUs, a (0x10) and b (0x11). Each serves an echo service of its own and calls the other's: all it takes to add a service.

```c
#define ECHO_CMD 0x80 /* applications use 0x80 and up */

static int echo(const struct mcubus_req *req, uint8_t *rsp)
{
	rsp[0] = req->src;
	memcpy(&rsp[1], req->data, req->len);
	return 1 + req->len;
}

MCUBUS_SERVICE_DEFINE(echo_service, ECHO_CMD, echo);
```

```c
mcubus_request(peer, ECHO_CMD, msg, len, rsp, 1 + len);
```

## Build and run

```
ws-zephyr/scripts/mcubus_run.sh ws-zephyr/samples/mcubus/hello a b
```

```
[b] <inf> hello: 0x10 answers: protocol 1
[b] <inf> hello: 0x10 echoed "hello from mcu-b@11", from 0x11
[a] <inf> hello: 0x11 answers: protocol 1
[a] <inf> hello: 0x11 echoed "hello from mcu-a@10", from 0x10
```

Twister: `west twister -p native_sim -T ws-zephyr/samples/mcubus/hello`
