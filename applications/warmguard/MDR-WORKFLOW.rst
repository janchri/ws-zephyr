WarmGuard: fictional medical-device workflow
============================================

**Everything in this scenario is invented for training.** WarmGuard does not
exist, 50 °C has no clinical justification, and no clinical study, regulatory
assessment, product approval or real patient use has occurred. Never use this
example to control a real warmer or as evidence of MDR conformity.

``west safety assess`` checks the structure of the fictional records in
`mdr-fixture.json <mdr-fixture.json>`__ and that their referenced files exist. It
can pass when the teaching fixture is complete. It does **not** check that a
record is true, that a test passed, that a person approved anything, or that a
real device meets the MDR. Actual test results and Affirmatrix reviews remain
separate and must be examined with ``west safety status --run NAME``.

The fictional device
--------------------

WarmGuard is a proposed bedside contact-warmer temperature monitor. In this
scenario a sensor reports an integer temperature to software which requests an
alarm at 50 °C or above. A fictional clinician is expected to investigate the
alarm. The sensor, warmer, display, audible alarm, communications, and clinical
environment are imaginary: this repository implements only the threshold
function, not the complete device.

The simulated regulatory owner identifies this as a potential medical device,
assumes it could influence a therapeutic device, and opens a classification
assessment. **No classification or notified-body route is determined**: that
requires a real intended purpose, full device description and application of
all relevant rules. The invented patient population, claims, contraindications,
operating conditions and user instructions would have to be specified before a
real assessment. See `MDCG 2019-11 rev. 1
<https://health.ec.europa.eu/document/download/b45335c5-1679-4c71-a91c-fc7a4d37f12b_en?filename=mdcg_2019_11_en.pdf>`__.

One illustrative risk-control chain
-----------------------------------

.. list-table::
   :header-rows: 1

   * - Fictional record
     - Example
   * - Hazard
     - Excessive heat from a contact warmer.
   * - Hazardous situation and possible harm
     - Excessive heat reaches a patient without a timely alarm; a burn may result.
   * - Candidate software control
     - ``DEMO-SW-001`` requires ``alarm_required`` to return true for inputs at or above 50 °C.
   * - Implementation
     - ``IMPL-alarm_required`` in `alarm.c <src/alarm.c>`__.
   * - Test
     - ``DEMO-TC-001`` checks 49, 50 and 51 °C in `main.c <tests/main.c>`__.
   * - Hypothetical review
     - The reviewer checks whether the requirement, code and test agree; ``affirm`` records the
       person's actual review of a graph link.
   * - Residual-risk decision
     - **Simulated only:** assume the exercise's reviewer labels the risk unacceptable until sensor
       failure, alarm delivery, timing and clinical threshold are addressed. No real acceptance
       decision is made.


The hazard and its link to ``DEMO-SW-001`` are narrative, not Affirmatrix nodes;
their completeness is not automatically checked. The graph covers the existing
requirement, implementation and test relationships. A ``Witnesses`` link is
inferred from shared requirements, not measured execution coverage.

Simulated process records
-------------------------

The entries below close the *teaching exercise's documentation slots* only. They
do not claim that the associated activities took place.

1. **Quality and planning.** Invented roles: a developer writes changes, a
   reviewer inspects links and results, and a quality lead checks the release
   file. A real manufacturer must assign competent people, controlled procedures,
   independence where required, training and a maintained quality system.
2. **Verification and validation.** The implemented unit test checks three
   threshold values. The fictional system test plan additionally calls for
   sensor faults, response time, alarm audibility, electrical safety, supported
   configurations, and use by the intended clinician. No corresponding system
   tests or real device validation have been performed.
3. **Clinical, usability and security.** The simulated clinical plan asks for
   justification of the threshold and evidence of benefit; the usability plan
   asks whether a clinician notices and understands an alarm; the security plan
   asks how a changed sensor input or software update could affect safety. These
   are plans, not clinical data, completed studies or security test results.
4. **Tools and dependencies.** The exercise uses Zephyr, Twister, Sphinx,
   StrictDoc and Affirmatrix. A real team must identify the exact versions and
   intended uses, evaluate tool failures and dependencies, and document needed
   controls. The captured run records some revisions, not a complete toolchain.
5. **Technical documentation.** The source requirements, code, test and the
   case repository illustrate traceable records. A real technical file also
   needs applicable safety and performance requirements, risk management,
   clinical evidence, product verification and validation, and other relevant
   Annex II and III information. This fixture is not that file.
6. **Release and post-market.** The invented quality lead would reject release
   because system and clinical evidence are missing. In the maintenance exercise,
   a report of a missed alarm would be logged, investigated, linked to affected
   versions, assessed for reporting, and followed by a controlled correction.
   No real release, complaint, investigation or surveillance system exists.

Run the exercise
----------------

Follow the `README <README.rst>`__ to run the unit test and inspect, affirm and
record the real example relationships. Run ``west safety assess`` to check the
fictional fixture's expected fields and local file references. Remove a record
or break a reference in ``mdr-fixture.json`` to see this check fail; restore it
when finished. A successful fixture check is **not** an unblocked evidence gate:
use ``west safety status --run NAME`` to inspect the actual graph and saved run.

The `MDR <https://eur-lex.europa.eu/eli/reg/2017/745/oj/eng>`__ covers manufacturer
obligations and technical documentation. `IEC 62304
<https://webstore.iec.ch/en/publication/22794>`__
addresses the software lifecycle, not validation and release of the whole device.
