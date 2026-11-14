:orphan:

Test report
===========

To generate a current result, install the WarmGuard tools and run
``west safety prepare`` from the workspace root. Then open the generated review
snapshot linked from WarmGuard. For a saved run, use
``west safety build --run NAME``. See :doc:`walkthrough` for the full sequence.

A normal ``west docs build`` does not run tests or select evidence. If no report
is available, the message below explains how to create one.

This document correlates the specification with actual Twister execution output.
A rendered passing result is execution evidence, distinct from a review of a link.
The test exercises boundary values; it is not exhaustive over all integer inputs.

.. testreport:: twister_report.xml
   :module: application.warmguard.alarm

.. twisterinfo:: twister.json
