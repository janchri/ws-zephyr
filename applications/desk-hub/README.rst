Desk Hub
========

Desk Hub is a Zephyr application in development for the `Waveshare ESP32-S3 4.2-inch RLCD Development
Board <https://www.waveshare.com/esp32-s3-rlcd-4.2.htm>`__. Use Zephyr's ``native_sim`` target for
testing on the host.

Desk Hub opens an app menu for its built-in apps. Game of Life runs on a grid
of 4 × 4 pixel cells, advances every 100 ms, and updates regions containing
changed cells. Calendar pairs a large UTC clock and today badge with a navigable month grid.
Its persistent LVGL widgets update the clock and seconds marker without rebuilding
the calendar each second.
The menu, apps, and simulator controls use LVGL; Zephyr's LVGL display
integration handles monochrome partial refreshes to the display driver.

Building
--------

Run commands from the west workspace root (the directory containing
``ws-zephyr`` and ``zephyr``) with the Zephyr Python environment activated.

Desk Hub starts in an app menu with tiles for Game of Life and Calendar.
Short left/right presses move between tiles; hold right to open the selected
app. Hold left from an app to return to the menu. In Game of Life, short
left/right presses move backward/forward through the seeds. In Calendar, they
move backward/forward through months.

Both apps are enabled by default. Set ``CONFIG_DESK_HUB_CONWAY=n`` or
``CONFIG_DESK_HUB_CALENDAR=n`` to omit either app.

Architecture
------------

To add an app, compile its model, view and controller and register the controller:

.. code-block:: c

   #include "app_registry.h"

   static int notes_run(void)
   {
       /* Create the view, handle events, and return 0 on long left. */
       return 0;
   }

   DESK_HUB_APP_REGISTER(app_c_notes, "NOTES", notes_run);

Apps with setup and cleanup can include optional hooks in their registration:

.. code-block:: c

   DESK_HUB_APP_REGISTER(app_a_conway, "GAME OF LIFE", desk_hub_conway_run,
                         .on_entry = conway_on_entry, .on_exit = conway_on_exit);

Add the sources with ``target_sources_ifdef(CONFIG_DESK_HUB_NOTES app PRIVATE ...)``
and define that Kconfig option. The linker iterable section is the only app list;
the selector requires no app-specific includes, switches or initialization calls.
Registration symbol names sort lexically and determine tile order. The selector
pages through four tiles at a time without allocating an array for every app.

Source ownership:

* ``src/apps/<app>/``: controller, model and LVGL view. The selector is an app too.
* ``src/core/app_registry.*``: iterable registrations and lookup.
* ``src/core/app_runtime.*``: service the UI and wait for normalized input.
* ``src/core/app_ui.*``: shared content container and LVGL label/rectangle helpers.
* ``src/core/event.*``: normalize physical button states into short/long events.
* ``src/platform/``: display setup, system clock, GPIO input and simulator toolbar.
* ``src/main.c``: initialize platform services and start the selector.

Models contain domain logic and have no LVGL or Zephyr dependencies. Conway's
controller supplies the random byte source. Models and views are single instances;
one app runs at a time. Controllers coordinate model updates and view rendering,
and call ``app_runtime_next_event()`` every iteration to service UI/input.

Views own layout and parent their widgets to ``app_ui_root()``. Coordinates start
at (0, 0) in the app area; the emulator toolbar is outside it. Controllers never
subtract toolbar pixels or compute canvas dimensions. The runtime invokes
``on_entry()``, ``on_run()`` and ``on_exit()`` in order and clears shared content
before entry and after exit. Only ``on_run`` is required. Entry failure skips
``on_run`` but still calls ``on_exit`` to release partially initialized resources.
Exit hooks must therefore tolerate incomplete setup. ``on_run`` returns 0 for
the menu or a negative errno on failure. The selector uses this same lifecycle;
its entry hook initializes the clock only when the system time is unset. All UI calls run
on the main thread. GPIO callbacks and work handlers only publish input events.

The Waveshare display is 400 × 312 in landscape. The native simulator uses a
400 × 360 display with a 48-pixel button toolbar above the app area.

native_sim
~~~~~~~~~~

Install the SDL2 development libraries on the host. SDL uses a monochrome
framebuffer in the same logical orientation as the game on Waveshare.

The SDL display enables ``CONFIG_DESK_HUB_EMULATOR`` automatically. Its
``SYS_INIT`` hook runs at APPLICATION priority 91, after LVGL (priority 90),
creates the toolbar and reserves its space before ``main()`` creates app content.
Hardware builds omit the emulator component and use the full display area.

The simulator toolbar buttons behave like the hardware buttons. Hold a button
to activate its long-press action.

Game of Life cycles through Achim's p144, Breeder, Achim's other p16, and
Random. Random creates a fresh grid each time it is selected.
Breeder places two Gosper glider guns near the center, firing down-right and
up-left.

The platform initializes an unset system clock from the firmware build
timestamp. The board has no configured persistent RTC or network time source,
so time resets to that timestamp after reboot unless another time source sets
the system clock.

.. code-block:: console

   west build -b native_sim/native/64 ws-zephyr/applications/desk-hub -d build/desk-hub-native
   west build -d build/desk-hub-native -t run

The emulator watches SDL show, expose and restore events and requests a fresh
LVGL frame. The first frame and restored windows do not require a button press.

Waveshare ESP32-S3-RLCD-4.2
~~~~~~~~~~~~~~~~~~~~~~~~~~~

In landscape with USB on the right, LVGL presents the app in landscape
orientation. Zephyr's LVGL display integration sends the rendered regions to
the standard ST7306 driver.

.. code-block:: console

   west build -b esp32s3_rlcd_4_2/esp32s3/procpu ws-zephyr/applications/desk-hub -d build/desk-hub-rlcd
   west flash -d build/desk-hub-rlcd

If an existing build still references the old board configuration, rebuild
with ``west build -p always`` before flashing.
