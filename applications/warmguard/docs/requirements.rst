:orphan:

Requirements
============

Start with the system goal, then read the two software rules below it. Each
card contains the authored statement and the relationships found in code and
test annotations. Click a linked ID to follow the requirement to its parent,
implementation or test.

Requirement statements
----------------------

.. strictdoc-requirements::

Follow the evidence
-------------------

.. list-table:: How to read the links
   :header-rows: 1
   :widths: 25 75

   * - Link
     - Meaning
   * - Refines / refined by
     - Connects the system goal to a more precise software requirement.
   * - Satisfied by
     - C implementation that declares it addresses this requirement.
   * - Verified by
     - Test specification that declares it checks this requirement.
   * - Covered by
     - Linked execution result, when a test report has been loaded.

A declared link helps you navigate; inspect its content and the review state
before treating it as evidence. A test result and a human review are separate.

.. needtable::
   :columns: id;title;refines as "Parent";satisfies_back as "Implementation";verifies_back as "Tests"
   :filter: type == "req" and not is_external

The statements come from
``ws-zephyr/applications/warmguard/docs/requirements.sdoc``. Edit that file to
change a requirement, then rebuild. Implementation and test links are extracted
from their source annotations, so the table is generated rather than maintained
by hand.

Review status
-------------

These are **relationship states**, computed by Affirmatrix when this site is
built. Rebuild the documentation after recording a review or changing source;
refreshing the browser alone does not recompute them. Test results appear on
the :doc:`test-report` page and are separate from human reviews.

.. affirmatrix-status::

* **pending**: no review has affirmed this relationship yet.
* **active**: the reviewed content still matches and dependencies are active.
* **directlyOutdated**: an endpoint's reviewed content changed.
* **transitivelySuspect**: the content matches, but a supporting relationship is suspect.
* **doublyOutdated**: content changed and supporting relationships are suspect.
* **broken**: an endpoint is missing.
* **vanished**: the current source no longer declares the recorded relationship.
