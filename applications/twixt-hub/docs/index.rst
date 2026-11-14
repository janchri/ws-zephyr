.. include:: ../README.rst

Where to start
--------------

Complete :doc:`/getting-started` before building. To explore without hardware,
run the simulated integration test below. It starts a gateway and simulated
MCUs on a shared virtual bus and checks communication between them.

For hardware, choose a gateway and nodes from the build commands above, inspect
their overlays, and connect the I2C bus as required by those configurations.
A successful build produces firmware; it does not exercise the physical bus.
After flashing, the shell commands above let you check that nodes respond and
that the addition service returns ``42``.

Twixt library and verification
------------------------------

This application uses the `twixt library <https://github.com/janchri/twixt>`_
from ``<rootdir>/modules/lib/twixt``. Twixt implements requests and responses
between MCUs over a shared I2C bus; the application configures the node and
gateway roles, settings, storage and management services. For service APIs,
devicetree configuration and library-specific examples, see the library's
``README.rst`` and its ``samples/`` and ``tests/`` directories.

The application's ``sample.yaml`` defines a multi-process ``native_sim``
integration test and hardware build-only configurations. Run the simulated
application and library suites from the workspace root:

.. code-block:: console

   west twister -p native_sim -T modules/lib/twixt -T ws-zephyr/applications/twixt-hub --outdir twister-out/twixt

The native test uses a simulated bus; it does not verify physical wiring or
radio behavior. The BLE management endpoint described above has no pairing in
the current twixt implementation; see the library documentation before using
it outside a controlled environment.
