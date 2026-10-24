# WirbelWind

Zephyr port of [MusicBox_ESP32a1s](https://github.com/janchri/MusicBox_ESP32a1s),
a Toniebox-like music box: place a tag, its playlist plays from where it
stopped last time; remove the tag and playback pauses.

## Architecture

Every module has its own header with just its interface. Code that only exists
for a platform lives in its own folder (`src/native/`, `src/esp32a1s/`).

| Module                       | Job                                                        | Arduino original |
|------------------------------|------------------------------------------------------------|------------------|
| `main.c`                     | Startup                                                    | `setup()`        |
| `events.c/.h`                | Event queue: tags, button, end of track, UI commands       | `EventQueue`     |
| `controller.c/.h`            | Box logic: tag → playlist, button, resume, volume          | `loop()`         |
| `inputs.c`                   | Button (gpio-keys) and volume pot (analog-axis) → events   | `Button`         |
| `player.c/.h`                | Player thread: SD → minimp3 → volume → audio output        | `SD_Player`      |
| `audio.h`                    | Audio output interface, implemented per platform           | `AudioOutputI2S` |
| `playlist.c/.h`              | JSON playlists on the SD card, same format/location as before | `Playlist`    |
| `resume.c/.h`                | Where each playlist continues (settings / NVS)             | tmp playlist (SPIFFS) |
| `storage.c/.h`               | SD card mount and paths                                    | `Board`          |
| `shell.c`                    | `ww` shell commands (on hardware also `ww tag`, bypassing the reader) |  |
| `http.c/.h`, `web/index.html`| REST API and web UI (embedded gzipped)                     | `Restserver`, `www/` |
| `wifi.c`                     | Hotspot at startup when no tag is on the box               | `Network`        |
| `native/audio.c`             | native_sim audio output, Zephyr side                       |                  |
| `native/audio_bottom.c`      | native_sim audio output, Linux side (popen/aplay)          |                  |
| `resample.c/.h`              | Sample rate conversion to a fixed output rate              |                  |
| `rfid/mfrc522.c/.h`          | MFRC522 driver (`dts/bindings/nxp,mfrc522.yaml`): card UID | `MFRC522` library |
| `rfid/reader.c`              | Polls the reader → tag placed/removed events               | `Rfid`           |
| `native/sim.c`               | Simulated button and pot (`ww button/pot`)                 |                  |
| `native/mfrc522_emul.c`      | Emulated MFRC522 + card on the SPI emulator (`ww tag`)     |                  |
| `esp32a1s/audio.c`           | ESP32-A1S audio output: I2S0 + MCLK on GPIO0               | `AudioOutputI2S` |
| `esp32a1s/ac101.c/.h`        | AC101 codec over I2C (`dts/bindings/x-powers,ac101.yaml`)  | `AC101` library  |

Dependencies point one way: the UIs (`shell`, `http`) and `inputs` post events
or call the controller; the controller uses `player`, `playlist` and `resume`;
the player uses `audio` and `storage`.

Playlists: `/Music/.config/playlist_<uid>` with
`{"uuid":"<uid>","name":"...","tracks":["/Music/...mp3", ...]}`.
An unknown tag gets an empty playlist automatically.

Controls:

- **Tag**: placed = play (from where it stopped), removed = pause. The box has no
  play/pause button (the web UI has one per track).
- **Button**: short press = next track; ≥1 s = restart the playlist from the
  first track; ≥10 s = forget the positions of all playlists.
- **Potentiometer** (analog input via `analog-axis`): volume 0..20.

## native_sim

```sh
west build -b native_sim -d build-native ws-zephyr/applications/wirbelwind

# SD card image with three playlists: tag 0a1b2c3d = test tones, a1b2c3d4 =
# "Six Little Ducklings" (LibriVox), b5c6d7e8 = Bach Goldberg Variations (Musopen).
# The two free recordings are downloaded once into ~/.cache/wirbelwind (-n skips them).
ws-zephyr/applications/wirbelwind/scripts/mk_sdcard.sh -o sdcard.img
# ... or with your own music instead: one playlist per UID=DIR
ws-zephyr/applications/wirbelwind/scripts/mk_sdcard.sh -o sdcard.img 0a1b2c3d=~/Music/Bibi

build-native/zephyr/zephyr.exe -uart_stdinout --sdcard=sdcard.img --flash=flash.bin \
    --audio_cmd="aplay -q -f S16_LE -r {rate} -c {ch}"
```

In the web UI every track has a play button (start it from the beginning: now,
or when its tag is placed next time); the track playing right now has a pause
button instead. Pausing from the web is the only pause besides removing the tag.

The volume starts at 0 like a pot turned fully down: run `ww pot 50` to hear
something.

Web interface: http://localhost:8080 (native_sim uses the host's sockets; port 80
on hardware). `#pl=<uid>` in the URL opens a playlist directly.

`--audio_file=out.raw` writes the raw PCM instead; without either option the
audio is discarded. The music lives in `sdcard.img`; `flash.bin` is the internal
flash holding only the saved positions, it is created on the first start.
Use ASCII file names (FatFs runs with code page 850, not UTF-8); the web upload
converts names automatically (ä → ae, ß → ss, ...).

```
uart:~$ ww tag place 0a1b2c3d     # card on the emulated reader: 4, 7 or 10 byte UID
uart:~$ ww status
uart:~$ ww button              # short press: next track
uart:~$ ww button 1500         # hold 1.5 s: restart playlist
uart:~$ ww button 10500        # hold 10.5 s: reset all playlists
uart:~$ ww pot 40              # turn the volume pot to 40 %
uart:~$ ww tag remove
uart:~$ ww pl list | show <uid> | create <uid> [name] | add <uid> </Music/dir-or-mp3>
uart:~$ ww pl name <uid> <name> | rm <uid> <nr> | del <uid>
uart:~$ ww next | reset | reset_all | vol [n] | seek <s> | track <nr> | ls [path]
```

## ESP32-A1S Audio Kit (AC101)

Ai-Thinker ESP32 Audio Kit v2.2 with the AC101 codec, built on top of the
`esp32_devkitc` board (`boards/esp32_devkitc_procpu.overlay` has the pins).

```sh
west build -b esp32_devkitc/esp32/procpu -d build-esp32 ws-zephyr/applications/wirbelwind
west flash -d build-esp32
west espressif monitor          # or: picocom -b 115200 /dev/ttyUSB0
```

- Audio: I2S0 master (BCLK 27, LRCK 26, DOUT 25) → AC101 (I2C 33/32), MCLK on
  GPIO0, speaker amplifier enable on GPIO21. The output always runs at 44.1 kHz
  stereo; other rates and mono are converted (`resample.c`).
  `CONFIG_WW_AC101_HP_VOLUME` / `CONFIG_WW_AC101_SPK_VOLUME` set the analog
  levels, the pot/`ww vol` scales on top of that.
- SD card: 4 bit SDMMC, FAT. The `sdcard.img` from `mk_sdcard.sh` can be
  written to a card (`dd`), or copy music to a FAT32 card and create the
  playlists with `ww pl create/add`.
- Button on GPIO22, volume pot on GPIO34 (ADC1 ch 6, **placeholder**: adjust to
  the wiring). An unconnected pot input floats; use `ww vol` then.
- RFID: MFRC522 on VSPI (SCK 18, MISO 19, MOSI 23, CS 5), 4 MHz, no reset
  pin. Polled every 250 ms; a card counts as removed after 3 polls without
  it. UIDs are lower case hex like before (`a1b2c3d4`, 7 and 10 byte UIDs too).
  `ww tag place <uid>` posts tag events directly, without a card.
- WiFi: if no tag is on the box 3 s after power-on, it opens the hotspot
  `music.Box` (like the Arduino version) and the web interface is at
  http://192.168.4.1 (DHCP for the phone/laptop). With a tag on the box at
  power-on there is no hotspot. Once open it stays until the next restart.
  The hotspot is **open** by default, like before: anyone nearby can use the
  web interface. Set `CONFIG_WW_WIFI_AP_PSK` (8..63 characters) for WPA2.
  Name, address and the waiting time: `CONFIG_WW_WIFI_AP_SSID`, `_IP`, `_DELAY_MS`.
- Memory: the WiFi driver keeps its heap in the 8 MB PSRAM of the A1S module
  (`CONFIG_ESP_SPIRAM`), internal RAM is too small for audio + WiFi + web.

## REST API

All JSON; paths are relative to the SD card.

| Request | Body / query | Result |
|---------|--------------|--------|
| `GET /api/status` | | `{present, uuid, name, track, tracks, path, state, pos, volume, volume_max}` |
| `POST /api/control` | `{"cmd":"pause\|resume\|next\|reset\|reset_all\|volume\|seek\|track","arg":n}` | `{ok}` |
| `GET /api/playlists` | | `[{uuid, name, tracks}]` |
| `GET /api/playlist` | `?uuid=` | `{uuid, name, tracks:[...], resume:{track, pos}}` |
| `POST /api/playlist` | `{"uuid", "name"?, "add"?, "remove"?, "track"?}` | playlist; creates it if needed. `track` plays that track from its start: right away if the tag is on the box, otherwise when it is placed next time (also overrides a paused position) |
| `DELETE /api/playlist` | `?uuid=` | `{ok}` (music files stay) |
| `GET /api/files` | `?path=/Music` | `[{name, dir, size}]` |
| `POST /api/files` | `?path=/Music/New` | create folder |
| `PUT /api/files` | `?path=/Music/a.mp3`, body = file | upload |
| `DELETE /api/files` | `?path=` | delete file or empty folder |

```sh
curl -X POST -d '{"cmd":"volume","arg":10}' localhost:8080/api/control
curl -T song.mp3 "localhost:8080/api/files?path=/Music/Bibi/song.mp3"
curl -X POST -d '{"uuid":"0a1b2c3d","add":"/Music/Bibi"}' localhost:8080/api/playlist
```

## Next steps

- Headphone detect (GPIO39) to switch the speaker amplifier off
- Joining a home WiFi (station mode, like the saved networks before)
- Firmware update via MCUboot
