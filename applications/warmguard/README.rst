WarmGuard: requirements, tests and review
=========================================

The `site guide <docs/index.rst>`__ and related documentation live in ``docs/``.

This fictional example shows how a requirement, C implementation, Zephyr test,
Affirmatrix review and saved evidence fit together. It is **not a medical
product or an MDR conformity assessment**: the 50 °C threshold has no medical
justification. See `the medical-device workflow <MDR-WORKFLOW.rst>`__ for the
illustrative risk-to-test chain and the work a real manufacturer must add.

The built site's WarmGuard landing page introduces the scenario. Its left
menu contains requirements, implementation, test specification, test report and
**Run and review WarmGuard**. A normal ``west docs build`` already shows the
requirements and source references; follow the steps below to add execution
results and a review snapshot.

Run every command from ``<rootdir>``, the west workspace root containing
``ws-zephyr/`` and ``zephyr/``. No hardware is needed.
For application documentation without WarmGuard's verification or assurance
workflow, see the `documentation commands <../../doc/README.rst>`__
(``west docs build`` and ``west docs show``).

Prepare and explore
-------------------

.. code-block:: sh

   source .venv/bin/activate
   west update zdocs
   pip install -r ws-zephyr/applications/warmguard/requirements.txt
   west safety prepare
   west docs show


``prepare`` runs the temperature test, builds the website and checks the
relationships. Open http://127.0.0.1:8000/html/workspace/ and follow the
requirements to the implementation, test, results and review states. Stop the
server with Ctrl+C. A fresh case has ``pending`` relationships: passing the test
does not mean a person reviewed them.

Capture a revision-bound test
-----------------------------

Review and commit the source changes you intend to keep first. Both ``ws-zephyr``
and ``zephyr`` must have clean Git working trees, including no untracked files.
Configure your Git identity. Choose a new name if ``baseline-001`` already exists.

.. code-block:: sh

   west safety record --case -m "Record baseline graph"
   west safety prepare --run baseline-001


This runs Twister, saves its reports and source revisions in
``assurance/warmguard/runs/baseline-001/``, rebuilds the website from
that run, and checks the evidence gate. The gate is expected to report blocked
and return nonzero while relationships are pending. If a test fails, its result
is preserved in the named run and ``prepare`` stops; inspect the Twister output.
``west safety build --run baseline-001`` will build its report afterwards.

Review the links
----------------

.. code-block:: sh

   west safety review
   west safety review --node DEMO-SW-001
   west safety review --node IMPL-alarm_required
   west safety review --node DEMO-TC-001


Compare the requirement (alarm at or above 50 °C), the implementation
(``temperature_c >= 50``), and the test of 49, 50 and 51 °C. Read the actual
result on the website. Review the remaining requirement and relationships too.
After reviewing a link, affirm it with your actual role and reason:

.. code-block:: sh

   west safety affirm --kind Implements --from IMPL-alarm_required --to DEMO-SW-001 \
     --role Developer --reason "Reviewed the threshold implementation against DEMO-SW-001"


Repeat for the other links you review. You can select ``--below DEMO-SYS-001``
only after reviewing every selected link and when your reason applies to all of
them. Affirming a link creates a draft human review; it does not change a test
result. Avoid ``--revision``: it bypasses source-revision and cleanliness checks.

Record and check
----------------

Preview the case and evidence first. The preview shows tracked differences and
lists new files; open the new files too before recording them.

.. code-block:: sh

   west safety record --case --run baseline-001 --preview
   west safety record --case --run baseline-001 -m "Record review and verification"
   west safety status --run baseline-001
   west docs show


``record`` immediately stages and commits the selected records with your Git
identity; it does not push. After all relationships are affirmed and evidence is
accepted, expect active relationships and an unblocked example evidence gate.
That gate checks the graph and run, not MDR conformity or device safety. Refresh
with ``west safety status`` or ``build`` before refreshing the browser. The review
page shows when its status was computed and which test report was selected.

The assurance repository is separate from ``ws-zephyr``. Back it up and
synchronize it independently. On a new workspace, clone it to
``assurance/warmguard/`` before building. In an existing clean case, use
``git -C assurance/warmguard pull --ff-only`` before work and push after
recording. Resolve divergence deliberately; do not overwrite existing records.

See the open medical-device work
--------------------------------

.. code-block:: sh

   west safety assess


This checks the completeness of the fictional exercise fixture: its records and
their file
references are present. Passing means the fixture is structurally complete; it
does not verify test results, human approvals, or MDR conformity. The
`workflow example <MDR-WORKFLOW.rst>`__ explains the simulated decisions and the
work still needed for a real device.

To try change detection, after affirming the baseline change ``>= 50`` to ``>= 51``
in ``src/alarm.c``, then run ``west safety build``, ``west safety status`` and
``west safety verify``. The test fails at 50 °C, implementation links become
outdated, and parent links become suspect. Restore the source afterward.

The source is in ``ws-zephyr/applications/warmguard/``; documentation output is
in ``<rootdir>/build/docs/``, Twister output in ``twister-out/``, and caches in
``tmp/``. Keep the persistent ``assurance/warmguard/`` repository separate
from build cleanup. ``west safety --help`` shows the individual commands.
