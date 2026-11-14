Run and review WarmGuard
========================

Start here after reading :doc:`/applications/warmguard`. No board is needed.
Commands run from the workspace root with ``.venv`` active. Install the Zephyr
SDK and the host documentation tools from :doc:`/getting-started` first.

Run the example
---------------

.. code-block:: console

   pip install -r ws-zephyr/applications/warmguard/requirements.txt
   west safety prepare
   west docs show

``prepare`` runs the native Zephyr threshold test, builds the documentation and
checks the relationship graph. Open http://127.0.0.1:8000/html/workspace/ and
choose WarmGuard. Follow its generated review snapshot links to see test
results and relationship states together.

Read :doc:`requirements`, follow a software requirement to
:doc:`implementation` and :doc:`test-specification`, then inspect
:doc:`test-report`. The test checks 49, 50 and 51 °C.

Expect pending reviews on a new case. A passing test means the selected inputs
passed; an active relationship means someone reviewed a link and its recorded
content still matches. Neither result establishes medical-device conformity.

Save a test run
---------------

For revision-bound evidence, first review and commit your source changes.
Both ``ws-zephyr`` and ``zephyr`` must have clean working trees, including no
untracked files. Configure your Git identity. Choose an unused run name:

.. code-block:: console

   west safety record --case -m "Record baseline graph"
   west safety prepare --run baseline-001

The run is saved under ``assurance/warmguard/runs/baseline-001/`` with test
reports and source revisions. A pending review can make the gate return nonzero
while preserving the run. If verification itself fails, inspect the saved test
output; render it with ``west safety build --run baseline-001``.

Review before affirming
-----------------------

.. code-block:: console

   west safety review
   west safety review --node DEMO-SW-001
   west safety review --node IMPL-alarm_required
   west safety review --node DEMO-TC-001

Inspect the actual requirement, code, test and result. After reviewing an
individual link, record your role and reason. For example:

.. code-block:: console

   west safety affirm --kind Implements --from IMPL-alarm_required --to DEMO-SW-001 --role Developer --reason "Reviewed the threshold implementation against DEMO-SW-001"

Repeat only for links you have reviewed. An affirmation creates a draft human
review; it does not alter test results. The review-state descriptions on
:doc:`requirements` explain pending, active and outdated relationships.

Record and refresh
------------------

.. code-block:: console

   west safety record --case --run baseline-001 --preview
   west safety record --case --run baseline-001 -m "Record review and verification"
   west safety status --run baseline-001

Preview tracked differences and open new files before recording. ``record``
stages and commits the selected assurance records with your Git identity; it
does not push. After all required relationships are reviewed and evidence is
accepted, the example gate can become unblocked. Refresh the browser after
``status`` finishes; the page displays its snapshot time and selected report.

Keep the records
----------------

``assurance/warmguard/`` is a separate Git repository containing persistent
reviews and run bundles. Back it up and synchronize it independently. On a new
workspace, clone your existing case there before building. Keep it when cleaning
``build/``, ``twister-out/`` or ``tmp/``.

For the fictional process-record exercise, run ``west safety assess`` and read
:doc:`mdr-workflow`. That command checks fixture structure and file references,
not test success or human approval. The full training narrative lives in
``ws-zephyr/applications/warmguard/MDR-WORKFLOW.rst``.
