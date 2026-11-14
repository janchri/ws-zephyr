ws-zephyr: applications and guides
==================================

This workspace contains independent Zephyr applications and the libraries needed
to build them. Start with setup, choose an application, then follow its guide.
All command examples run from the directory containing ``ws-zephyr/`` and
``zephyr/`` with the workspace Python environment activated.

Choose an application
---------------------

.. list-table::
   :header-rows: 1
   :widths: 20 50 30

   * - Application
     - What you can explore
     - What you need
   * - :doc:`applications/twixt-hub`
     - Requests between MCUs, gateways, storage and firmware management
     - Native simulation or supported boards
   * - :doc:`applications/wirbelwind`
     - RFID playlists, MP3 playback and music-box controls
     - Native simulation or an ESP32-A1S Audio Kit
   * - :doc:`applications/temperature-monitor`
     - Send DS18B20 temperature readings to MQTT
     - ESP32, sensors, Wi-Fi credentials and a broker
   * - :doc:`applications/warmguard`
     - Follow a requirement through code, tests, results and review
     - Native simulation; no hardware

New to the repo? Read :doc:`getting-started`. Want to understand the requirements
pages? Open :doc:`applications/warmguard`, then its :doc:`warmguard/requirements`.
Requirements and specifications are available in a normal documentation build.
Running tests and generating human-review snapshots is a separate step described
in the :doc:`warmguard/walkthrough`.

.. toctree::
   :maxdepth: 1
   :caption: Start here

   getting-started

.. toctree::
   :maxdepth: 2
   :caption: Applications

   applications/twixt-hub
   applications/wirbelwind
   applications/temperature-monitor
   applications/warmguard
