ws-zephyr
=========

A Zephyr workspace with four applications: a multi-MCU hub, an RFID music box,
a temperature-to-MQTT prototype, and a small requirements and testing exercise.
The manifest selects Zephyr and the libraries these applications use.

Choose where to start
---------------------

.. list-table::
   :header-rows: 1

   * - Application
     - What to expect
     - Start here
   * - Twixt Hub
     - I2C communication between MCUs, gateways, storage and firmware management; native simulation
       and hardware configurations
     - `Application guide <applications/twixt-hub/README.rst>`__
   * - WirbelWind
     - RFID-controlled MP3 player; try the native simulator before using an ESP32-A1S Audio Kit
     - `Application guide <applications/wirbelwind/README.rst>`__
   * - Temperature Monitor
     - ESP32, DS18B20 sensors, Wi-Fi and MQTT; needs local credentials, wiring and a broker
     - `Application guide <applications/temperature-monitor/docs/index.rst>`__
   * - WarmGuard
     - No-hardware exercise connecting requirements, C code, a boundary test and human reviews
     - `Walkthrough <applications/warmguard/README.rst>`__


These applications have different hardware and test coverage. WarmGuard implements
only a temperature-threshold function; its medical-device scenario is fictional.

Set up a new workspace
----------------------

Install the host tools described in the `Zephyr getting-started guide
<https://docs.zephyrproject.org/latest/develop/getting_started/index.html>`__.
Use Bash on Linux (or a suitably configured WSL environment). Create a directory
for the workspace, then run:

.. code-block:: sh

   python3 -m venv .venv
   source .venv/bin/activate
   pip install west
   git clone https://github.com/janchri/ws-zephyr.git
   west init -l ws-zephyr
   west update
   west zephyr-export
   west packages pip --install
   west sdk install


Run the commands in these guides from this **workspace root**, the directory
containing ``.west/``, ``ws-zephyr/`` and ``zephyr/``. Do not change into the Zephyr
clone. In each new shell, activate ``.venv`` again. ESP32 builds also need:

.. code-block:: sh

   west blobs fetch hal_espressif


The `getting-started guide <doc/getting-started.rst>`__ explains the directory layout,
application choices and common setup problems.

Build and read the documentation
--------------------------------

Install CMake, Ninja, Doxygen and Graphviz using your host's package manager.
The documentation uses the pinned ``zdocs`` project fetched by ``west update``.

.. code-block:: sh

   pip install -r ws-zephyr/doc/requirements.txt
   west docs build
   west docs show


Open http://127.0.0.1:8000/html/workspace/. The left menu contains setup,
application guides and WarmGuard's requirements, implementation and test
specification. This build does not run application tests. Stop the server with
Ctrl+C.

Try requirements and tests without hardware
-------------------------------------------

With the Zephyr SDK and documentation tools installed:

.. code-block:: sh

   pip install -r ws-zephyr/applications/warmguard/requirements.txt
   west safety prepare
   west docs show


Open WarmGuard in the site, then follow its requirements to the implementation,
test specification and test report. ``prepare`` runs the native test, builds the
site and checks the review state. Expect **pending reviews** and a blocked gate
until a person reviews the relationships. A passing test and an approved review
are separate results. The `WarmGuard walkthrough <applications/warmguard/README.rst>`__
explains saved runs, review and recording.

Keep source, output and review records separate
-----------------------------------------------

.. list-table::
   :header-rows: 1

   * - Path under the workspace root
     - Purpose
   * - ``ws-zephyr/``
     - This repository: applications, guides and west commands
   * - ``zephyr/``, ``modules/``, ``tools/``
     - Projects fetched by the manifest
   * - ``build/``, ``twister-out/``, ``tmp/``
     - Generated sites, build output, test output and caches
   * - ``assurance/warmguard/``
     - Persistent review records and saved evidence, in a separate Git repository


Back up and synchronize the assurance repository independently. Keep it when
cleaning build output. See `documentation operations <doc/README.rst>`__ for build
locations and commands.

Contribute
----------

C uses Zephyr's ``.clang-format``; Python uses the repository's Ruff configuration.
Install the pinned checks and the local pre-commit hook:

.. code-block:: sh

   pip install -r ws-zephyr/requirements.txt
   (cd ws-zephyr && pre-commit install)
   (cd ws-zephyr && pre-commit run --all-files)


The hooks can format files; review and stage those edits before committing.
Choose the relevant application test rather than assuming one test covers the
whole workspace. Workflow checks run with:

.. code-block:: sh

   python -m unittest discover -s ws-zephyr/tests/workflow -v
