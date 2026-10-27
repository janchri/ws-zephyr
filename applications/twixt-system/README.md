# twixt system

The twixt samples together, as a product would have them: three boards (a, b, c) and a console gateway (gw). Each board has

- its shell on a UART over the bus to the gateway (as in `samples/uart` of twixt),
- the setting `mcu/name`, and `twixt get` / `set` in the shell,
- settings in NVS (`settings` partition) and littlefs at `/lfs` (`storage` partition),
- MCUmgr on the shell with the fs and img groups (as in `samples/smp` of twixt).

## Build and run

```
modules/lib/twixt/scripts/twixt_run.sh ws-zephyr/applications/twixt-system a b c gw
```

Open a board's PTY (the gateway prints it) with `tio`:

```
mcu-a@10:~$ twixt get 0x11 mcu/name
0x11 mcu/name = "mcu-b@11"
mcu-a@10:~$ fs write /lfs/hello -o 0 68 69 0a
mcu-a@10:~$ fs cat /lfs/hello
hi
```

Or, with `tio` closed, MCUmgr:

```
smpmgr --port /dev/pts/7 file upload notes.txt /lfs/notes.txt
smpmgr --port /dev/pts/7 image upload zephyr.signed.bin
smpmgr --port /dev/pts/7 image state-read
```

smpmgr warns that reading the MCUmgr parameters failed: the boards have no os group. Images must be signed for MCUboot (`west sign`, `imgtool create`); on native_sim they land in slot 1, but nothing boots them.

Twister: `west twister -p native_sim -T ws-zephyr/applications/twixt-system`

The gateway also builds for the Raspberry Pi Pico (`-b rpi_pico -DMCU=gw`), see `samples/uart` of twixt.

The west projects `littlefs`, `mcuboot` and `zcbor` are needed.
