WirbelWind Music Box
====================

WirbelWind is a Zephyr music box: an RFID tag selects a playlist, playback
resumes from the saved position, and a button, potentiometer, shell, and web
interface provide controls. It is a port of `MusicBox_ESP32a1s
<https://github.com/janchri/MusicBox_ESP32a1s>`_. The full application guide,
including hardware pins and the REST API, is in
``ws-zephyr/applications/wirbelwind/README.rst``.

Before you start
----------------

Complete :doc:`/getting-started`. The native simulator is the quickest first run:
no RFID reader or audio board is needed. It creates a virtual SD card and lets
you place tags from the shell. Audible playback needs an audio output option;
otherwise you can still inspect playback state and the web interface.

How it is organized
-------------------

``inputs`` and the RFID reader post events; ``controller`` chooses the
playlist and sends work to ``player``. The player reads MP3 files from storage,
uses minimp3 to decode them and sends audio to the platform-specific output.
Playlist positions are saved through Zephyr settings. The web UI and shell
provide additional controls. ``src/native/`` implements simulated inputs and
audio; ``src/esp32a1s/`` drives the AC101 audio hardware.

Try the simulator
-----------------

From ``<rootdir>``, with the workspace set up and the virtual environment active:

.. code-block:: console

   west build -b native_sim -d build/wirbelwind-native ws-zephyr/applications/wirbelwind
   ws-zephyr/applications/wirbelwind/scripts/mk_sdcard.sh -n -o build/wirbelwind-sdcard.img
   build/wirbelwind-native/zephyr/zephyr.exe -uart_stdinout --sdcard=build/wirbelwind-sdcard.img --flash=build/wirbelwind-flash.bin

The simulator starts with volume at zero. At the shell use ``ww pot 50``,
``ww tag place 0a1b2c3d``, ``ww status`` and ``ww tag remove``. Without an
``--audio_cmd`` or ``--audio_file`` option, audio output is discarded. The web
interface is at http://localhost:8080. The image generator's ``-n`` avoids
fetching the optional online recordings; see the application README for audio
output, playlists and further commands.

Hardware
--------

The ESP32-A1S Audio Kit configuration uses an AC101 codec, SD card and MFRC522
reader; the overlay documents the actual pin assignments. Build with:

.. code-block:: console

   west build -b esp32_devkitc/esp32/procpu -d build/wirbelwind-esp32 ws-zephyr/applications/wirbelwind

The web access point is open by default; configure ``CONFIG_WW_WIFI_AP_PSK``
for WPA2 when appropriate. The application has a simulator but no Twister test
suite in its application directory. Simulator behavior does not validate the
physical audio, reader, storage or network hardware.
