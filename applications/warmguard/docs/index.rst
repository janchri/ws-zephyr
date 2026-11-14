WarmGuard
=========

.. include:: mdr-workflow.rst
   :start-after: ===============================

What you will find here
-----------------------

This example connects a small piece of software to its requirements and tests.
Read the pages in this order:

#. **Requirements:** the system goal and two precise software rules, with links
   to the code and tests that address them.
#. **Design and implementation:** the threshold function extracted from C source.
#. **Test specification:** what the native test checks, independent of execution.
#. **Test report:** the result of the selected run, when tests have been executed.
#. **Run and review:** how to generate results, inspect links and save reviews.

The requirements and code/test references are generated in every documentation
build. To also run the test and generate a review snapshot, follow
:doc:`/warmguard/walkthrough`:

.. code-block:: console

   pip install -r ws-zephyr/applications/warmguard/requirements.txt
   west safety prepare
   west docs show

The source is in ``ws-zephyr/applications/warmguard/``. The application runs on
``native_sim``; it implements a decision function rather than a physical sensor
or alarm. Review records and saved runs live in the separate
``assurance/warmguard/`` repository.

Explore WarmGuard
-----------------

.. toctree::
   :maxdepth: 1

   ../warmguard/mdr-workflow
   ../warmguard/requirements
   ../warmguard/implementation
   ../warmguard/test-specification
   ../warmguard/test-report
   ../warmguard/walkthrough

.. only:: not warmguard_assurance

   Generated review snapshot
   -------------------------

   .. warmguard-safety-links::
