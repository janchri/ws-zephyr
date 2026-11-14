:orphan:

Medical-device workflow example
===============================

WarmGuard explores a bedside contact-warmer scenario: read a temperature,
decide whether it is too high, and request an alarm. This repository implements
only the decision: ``alarm_required`` returns true at or above 50 °C and false
below it. A native test checks the values 49, 50 and 51 °C.

The purpose is to show how a system goal becomes software requirements, how
code and tests link to those requirements, and how a person records a review.
You can follow the complete example without hardware. The sensor, warmer and
physical alarm are imaginary; the 50 °C threshold has no medical justification.
This is a fictional training example, not a medical device or evidence of MDR
conformity.

A possible failure to investigate
---------------------------------

A high reading could be present without an alarm indication. Whether that
creates patient harm depends on an intended use, sensor, user, device, and
clinical environment; none is defined here.

``DEMO-SW-001`` states the example threshold. ``IMPL-alarm_required`` implements
it, and ``DEMO-TC-001`` checks 49, 50, and 51 °C. Follow the links on the
:doc:`/warmguard/requirements` page and the results on the
:doc:`/warmguard/test-report` page. A human review of a link is separate from the test result.

This hypothetical hazard is explanatory text, not a reviewed risk-control
record or an Affirmatrix node. The test does not verify sensor behavior,
response time, or alarm hardware. Residual risk and clinical benefit have not
been assessed.

What remains open
-----------------

A real device needs an intended purpose and classification, a quality system,
product risk management, verification and validation of the complete device,
clinical evaluation, cybersecurity, controlled release, and post-market
processes, as applicable. The exercise describes fictional records for these
topics; it does not perform those activities. ``west safety assess`` checks
that the fictional records and local file references are present. It does not
verify their truth, any test outcome, or conformity.

See ``ws-zephyr/applications/warmguard/MDR-WORKFLOW.rst`` for the full
training walkthrough and links to the regulation and software lifecycle standard.
