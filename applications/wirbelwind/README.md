# WirbelWind

Zephyr port of [MusicBox_ESP32a1s](https://github.com/janchri/MusicBox_ESP32a1s),
a Toniebox-like music box: place a tag, its playlist plays from where it
stopped last time; remove the tag and playback pauses.

## Architecture

| File                        | Job                                                        | Arduino original |
|-----------------------------|------------------------------------------------------------|------------------|
| `src/wirbelwind.h`          | All internal interfaces                                    |                  |
| `src/main.c`                | Startup, event queue, SD card mount                        | `Board`, `EventQueue` |
| `src/controller.c`          | Box logic (tag → playlist, keys, resume), button presses, persistent state (NVS) | `loop()`, `Button` |
| `src/playlist.c`            | JSON playlists on the SD card, same format/location as before | `Playlist`    |
| `src/player.c`              | Player thread: SD → minimp3 → volume → audio output        | `SD_Player`      |
| `src/audio_native*.c`       | native_sim audio output (host side in `_bottom.c`)         | `AudioOutputI2S` |
| `src/shell.c`               | `ww` shell; simulated RFID reader and keys on native_sim   | `Rfid`           |
| `src/http.c`, `web/index.html` | REST API and web UI (embedded gzipped)                  | `Restserver`, `www/` |

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
uart:~$ ww tag place 0a1b2c3d
uart:~$ ww status
uart:~$ ww button              # short press: next track
uart:~$ ww button 1500         # hold 1.5 s: restart playlist
uart:~$ ww button 10500        # hold 10.5 s: reset all playlists
uart:~$ ww pot 40              # turn the volume pot to 40 %
uart:~$ ww tag remove
uart:~$ ww pl list | show <uid> | create <uid> [name] | add <uid> </Music/dir-or-mp3>
uart:~$ ww pl name <uid> <name> | rm <uid> <nr> | del <uid>
uart:~$ ww next | reset | reset-all | vol [n] | seek <s> | track <nr> | ls [path]
```

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

- Hardware (ESP32-A1S audio kit): board overlay with SDMMC disk `SD`,
  gpio-keys (one button), analog-axis on an ADC pin for the pot, I2S audio sink + AC101 codec over I2C, PA enable on GPIO21,
  MFRC522 driver on SPI posting `WW_EV_TAG_*`
- WiFi (station + access point "music.Box" like before) and its settings page
- Firmware update via MCUboot
