# MCUmgr over the mcubus

A board (a) and a gateway (gw). MCUmgr runs on the board's shell, which is on a UART over the bus to the gateway (as in `../uart`). Configuration only: `board.conf` enables MCUmgr with the shell transport and the os group.

```
 smpmgr ── pty-a ── uart-bridge ── bus-uart-a ══ I2C ══ bus-uart ── shell ── MCUmgr
```

## Build and run

```
ws-zephyr/scripts/mcubus_run.sh ws-zephyr/samples/mcubus/smp a gw
```

With the PTY the gateway prints, and `smpmgr` (`uv tool install smpmgr==0.19.1`):

```
smpmgr --port /dev/pts/7 os echo "hello over the bus"
```

Twister: `west twister -p native_sim -T ws-zephyr/samples/mcubus/smp`

More groups (files, firmware images): `applications/mcubus-system`.
