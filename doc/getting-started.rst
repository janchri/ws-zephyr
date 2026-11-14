Getting started
===============

What this repository provides
-----------------------------

``ws-zephyr`` is the manifest and application repository inside a west workspace.
``west`` fetches Zephyr and the selected dependencies from ``west.yml``. Each
application has its own configuration and build directory; building one does
not build or test the others.

Set up once
-----------

Install the host dependencies from the `Zephyr getting-started guide
<https://docs.zephyrproject.org/latest/develop/getting_started/index.html>`_.
Firmware builds need the Zephyr SDK. Documentation builds additionally need
CMake, Ninja, Doxygen and Graphviz (``dot``) on your ``PATH``.

In an empty workspace directory, run:

.. code-block:: console

   python3 -m venv .venv
   source .venv/bin/activate
   pip install west
   git clone https://github.com/janchri/ws-zephyr.git
   west init -l ws-zephyr
   west update
   west zephyr-export
   west packages pip --install
   west sdk install

The manifest selects the Zephyr revision; use that checkout for these examples.
For ESP32 hardware, also run ``west blobs fetch hal_espressif``.

Where commands run
------------------

Keep your shell at the workspace root:

.. code-block:: text

   workspace/
     .venv/                 Python tools
     .west/                 west workspace configuration
     ws-zephyr/             this repository
     zephyr/                Zephyr checkout
     modules/               application and hardware libraries
     tools/                 documentation tools and other projects
     build/                 firmware and documentation output
     twister-out/           test output
     tmp/                   temporary files and caches
     assurance/warmguard/   persistent review records and saved runs

In each new shell, run ``source .venv/bin/activate`` from this directory.
Optionally keep temporary files local with:

.. code-block:: console

   mkdir -p tmp/cache
   export TMPDIR="$PWD/tmp" XDG_CACHE_HOME="$PWD/tmp/cache"

Read the site
-------------

.. code-block:: console

   pip install -r ws-zephyr/doc/requirements.txt
   west docs build
   west docs show

Open http://127.0.0.1:8000/html/workspace/. The build produces application guides,
requirements and generated code/test references. It does not execute tests or
approve relationships. Stop the server with Ctrl+C.

Choose a first run
------------------

* For requirements and a small native test, follow :doc:`applications/warmguard`
  and :doc:`warmguard/walkthrough`.
* For an interactive application without boards, try the
  :doc:`WirbelWind simulator <applications/wirbelwind>`.
* For communication between MCUs, start with :doc:`applications/twixt-hub`.
* For real temperature readings over MQTT, configure the hardware and credentials
  in :doc:`applications/temperature-monitor`.

Build commands in each application guide use a separate ``-d build/...`` path.
Read the selected board overlay before wiring or flashing hardware.

If something fails
------------------

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Symptom
     - What to check
   * - ``west`` cannot find the workspace or custom command
     - Activate ``.venv``, run from the workspace root, and check
       ``west topdir``. Initialize using ``west init -l ws-zephyr`` once.
   * - Missing module or Python package
     - Run ``west update`` and install the requirements for the command you use.
   * - Missing Doxygen, ``dot`` or compiler
     - Install the host documentation tools or the Zephyr SDK as appropriate.
   * - No test results on the site
     - ``west docs build`` only builds guides. Run ``west safety prepare`` for
       WarmGuard's test and review snapshot.
   * - Pending reviews or a blocked evidence gate
     - Read :doc:`warmguard/walkthrough`. A passing test does not affirm a link.
   * - Browser shows an old review state
     - Run ``west safety status`` to rebuild the snapshot, then refresh.

Generated output can be recreated; ``assurance/warmguard/`` contains persistent
work. Commit and back it up separately from this source repository.
