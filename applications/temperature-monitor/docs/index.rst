Temperature Monitor
===================

Temperature Monitor is an ESP32-oriented work in progress. It searches for
DS18B20 sensors on a 1-Wire bus, periodically publishes their measurements on a
Zephyr zbus channel, connects to Wi-Fi and sends temperature messages to an
MQTT broker. It is a separate application from the fictional requirements
example; it is not a medical-device implementation.

Before you start
----------------

Prepare an ESP32 board, DS18B20 sensor wiring, a 2.4 GHz Wi-Fi network and an
IPv4 MQTT broker reachable from that network. Complete :doc:`/getting-started`
and fetch the Espressif blobs first. This prototype needs hardware; use
:doc:`warmguard` for the no-hardware requirements exercise.

Data flow
---------

``w1_ds18b20.c`` discovers family-code ``0x28`` devices and schedules a sensor
poll every 60 seconds. It publishes sensor values and ROM identifiers on
``ds18b20_value_chan``. ``mqtt_conn.c`` subscribes, formats each value and
publishes it to a ``sensor/<ROM>`` topic using MQTT over unencrypted TCP port
1883. ``wifi.c`` starts the station connection and schedules reconnection after
a disconnect. ``main.c`` logs startup.

The ESP32 board overlay places the 1-Wire GPIO on GPIO4 and enables Wi-Fi; check
the board and sensor wiring for your hardware. The application's build selects
``mqtt_conn.c``; ``mqtt_conn_new.c`` is not in its CMake source list.

Configuration and build
-----------------------

The current sources include a local ``src/network/credentials.h`` with
``CONFIG_WIFI_SSID``, ``CONFIG_WIFI_PSK``, ``MQTT_SERVER`` and ``MQTT_CLIENT``.
That file is not supplied in the repository: configure it for your environment
before attempting a build and keep secrets out of version control. For example,
create the header with your own values:

.. code-block:: c

   #define CONFIG_WIFI_SSID "your-network"
   #define CONFIG_WIFI_PSK "your-password"
   #define MQTT_SERVER "192.0.2.10"
   #define MQTT_CLIENT "temperature-monitor"

The broker
address is parsed as IPv4. Dependencies, network credentials, and a broker are
needed before testing the application end to end.

From ``<rootdir>``, after setting up the workspace and ESP32 dependencies:

.. code-block:: console

   west build -b esp32_devkitc/esp32/procpu -d build/temperature-monitor ws-zephyr/applications/temperature-monitor

After building, flash with ``west flash -d build/temperature-monitor``.
Watch the serial log for Wi-Fi and MQTT connections and use your broker's client
to subscribe to ``sensor/#``. Expect one topic per sensor ROM and a temperature
payload formatted to three decimal places. Sensor polling is scheduled every
60 seconds; check connections and logs if no messages arrive.

This directory currently has no Twister test specification. A successful build
alone does not establish that sensor sampling, Wi-Fi or MQTT work on hardware.
