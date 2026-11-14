:orphan:

Design and implementation
=========================

The alarm decision is a stateless pure function over an integer temperature in
whole degrees Celsius. It returns true at or above 50 C and false below it.
Sensor sampling, calibration, invalid readings and alarm hardware are outside
this example. Its assumptions are that the caller supplies a valid integer
measurement in the specified units and handles the returned decision.

The implementation item below is extracted from the native Doxygen
``@satisfies`` annotations on ``alarm_required``.

.. symbolneeds::
