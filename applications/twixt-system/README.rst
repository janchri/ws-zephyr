twixt system
============

Pico USB gateway, nRF BLE gateway, and ESP32 node on one twixt I2C bus.
Run commands from the ``ws-zephyr`` repository root with the Zephyr Python
environment activated.

Building
--------

Use ``bd.conf`` for a node and ``gw.conf`` for a gateway. Board overlays are
selected automatically. These commands build the applications and MCUboot;
the nRF build also includes the network-core BLE controller.

.. code-block:: console

   west build --sysbuild -b rpi_pico/rp2040/mcuboot ws-zephyr/applications/twixt-system -d build/twixt-pico -- -Dtwixt-system_CONF_FILE=gw.conf
   west build --sysbuild -b nrf7002dk/nrf5340/cpuapp ws-zephyr/applications/twixt-system -d build/twixt-nrf-ble -- -Dtwixt-system_CONF_FILE=gw.conf
   west build --sysbuild -b esp32_devkitc/esp32/procpu ws-zephyr/applications/twixt-system -d build/twixt-esp32 -- -Dtwixt-system_CONF_FILE=bd.conf

Flashing
--------

Close serial terminals before flashing.

Pico: MCUmgr
~~~~~~~~~~~~

The Pico must already run MCUboot and an application supporting MCUmgr.
With the Python ``smpmgr`` client:

.. code-block:: bash

   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 image upload build/twixt-pico/twixt-system/zephyr/zephyr.signed.bin
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 image state-read

Copy the uploaded image's hash, then test it:

.. code-block:: bash

   IMAGE_HASH='paste-uploaded-image-hash-here'
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 image state-write "$IMAGE_HASH"
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 os reset

After USB reconnects, verify that the expected image is active before
confirming it:

.. code-block:: bash

   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 image state-read
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 os echo hello
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 image state-write "$IMAGE_HASH" --confirm
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if00 image state-read

In WSL, reattach the USB device after reset if necessary. If uploads time
out, use smaller packets and pace fragments. The validated Python
``smpclient`` settings were ``BufferSize(90)``, ``use_sha=False``, and a
10 ms pause between acknowledged chunks, followed by image-hash verification.

nRF: nrfutil
~~~~~~~~~~~~

Install Nordic ``nrfutil`` with its ``device`` command:

.. code-block:: console

   nrfutil install device

Flash the complete sysbuild, including MCUboot, the network-core BLE
controller, and the signed application. Replace ``1050750299`` with your
probe's serial number:

.. code-block:: console

   west flash -d build/twixt-nrf-ble -r nrfutil --dev-id 1050750299

ESP32: USB serial
~~~~~~~~~~~~~~~~~

Use your adapter's device path:

.. code-block:: console

   west flash -d build/twixt-esp32

Usage
-----

The Pico exposes one serial port per MCU:

.. list-table:: USB serial ports
   :header-rows: 1

   * - MCU
     - I2C address
     - Device
   * - Pico
     - ``0x70``
     - ``/dev/serial/by-id/usb-twixt_twixt_pico-if00``
   * - nRF
     - ``0x11``
     - ``/dev/serial/by-id/usb-twixt_twixt_pico-if02``
   * - ESP32
     - ``0x12``
     - ``/dev/serial/by-id/usb-twixt_twixt_pico-if04``

Open the Pico shell:

.. code-block:: console

   tio /dev/serial/by-id/usb-twixt_twixt_pico-if00

Run these commands in the shell; the addition returns ``42``:

.. code-block:: console

   twixt ping 0x11
   twixt ping 0x12
   twixt get 0x12 mcu/name
   twixt set 0x12 mcu/label desk
   twixt add 0x12 20 22

Open ``-if02`` or ``-if04`` with ``tio`` to use the nRF or ESP32 shell.
Nodes provide persistent settings and littlefs at ``/lfs``.

Close ``tio`` before using MCUmgr. All MCUs support image management and the
OS group (echo, reset, and parameter queries); nodes also support file
management. For example:

.. code-block:: console

   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if04 os echo hello
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if04 image state-read
   smpmgr --port /dev/serial/by-id/usb-twixt_twixt_pico-if04 os reset

For BLE management, connect an SMP client to ``twixt-nrf``, ``twixt-pico``,
or ``twixt-esp32``. The nRF forwards requests for the other MCUs over I2C.

.. code-block:: console

   smpmgr --ble twixt-pico --timeout 30 os echo hello
   smpmgr --ble twixt-pico --timeout 30 image state-read
   smpmgr --ble twixt-pico --timeout 30 os reset