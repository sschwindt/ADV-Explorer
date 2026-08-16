Installation
============

ADV-Explorer ships as ready-to-run executables. There is nothing to compile and **no administrator (admin/root) rights are required**: it runs from any folder in your user profile.

Requirements
------------

* **Windows**: Windows 10 or 11, 64-bit (x86_64). About 150 MB of disk space.
* **Linux**: a 64-bit (x86_64) Debian12+-like distribution from roughly 2022 or
  newer, for instance Ubuntu 22.04+, or Linux Mint 21+.
* ADV-Explorer is stand-alone and does **not need Python, Matlab, or other software**.
* An internet connection is optional. It is used only to fetch map tiles in field mode; everything else, including the coordinate grid and all analysis, works offline.

Windows 10/11
-------------

1. Download ``adv-explorer-windows-x86_64.zip`` from the
   `releases page <https://github.com/sschwindt/ADV-Explorer/releases>`_.
2. Right-click the zip file and select *Extract All...* (any target folder
   works, for example ``Documents\ADV-Explorer``; no admin rights needed).
3. Open the extracted folder and double-click ``adv-explorer.exe``.

.. note::

   Windows SmartScreen may warn about an unrecognized app the first time.
   Click *More info* and then *Run anyway*. This happens because the
   executable is not code-signed, not because anything is wrong with it.

Linux
-----

1. Download ``adv-explorer-linux-x86_64.AppImage`` from the
   `releases page <https://github.com/sschwindt/ADV-Explorer/releases>`_.
2. Make it executable and run it (no root rights needed):

   .. code-block:: bash

      chmod +x adv-explorer-linux-x86_64.AppImage
      ./adv-explorer-linux-x86_64.AppImage

   Alternatively, right-click the file in your file manager, enable
   *Properties > Permissions > Allow executing file as program*, and
   double-click it.

.. note::

   AppImages need the ``libfuse2`` library, which most desktop distributions
   ship by default. If the AppImage does not start and you cannot (or do not
   want to) install packages, run it without FUSE:

   .. code-block:: bash

      ./adv-explorer-linux-x86_64.AppImage --appimage-extract-and-run

Updating
--------

Simply download the newest release file and replace the old one. Project files written by an older version keep opening: the format is versioned and older layouts are read unchanged. A project written by a *newer* version than the one you are running is refused with a clear message rather than opened partially.

Building from source
--------------------

Only needed by developers who want to extend the application; see
:doc:`developer`.
