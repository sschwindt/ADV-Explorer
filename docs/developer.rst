Developer guide
===============

This page is only relevant if you want to build ADV-Explorer from source or
extend it. Regular users should install the packaged executables instead
(see :doc:`installation`).

Requirements
------------

* CMake >= 3.16 and a C++17 compiler (GCC >= 10 or MSVC >= 2019)
* Qt 6.2 or newer development packages (Widgets, Concurrent, PrintSupport,
  Test, and the private headers needed by QXlsx)
* Qt 6 Network and Sql, both part of ``qtbase``, for the field-mode basemap
  and for reading GeoPackage files. No PROJ or GDAL is needed: the map
  projections are implemented in ``src/core/Crs.cpp``.
* The Qt 6 SQLite **driver plugin**. On Debian and Ubuntu this is the separate
  package ``libqt6sql6-sqlite``, which ``qt6-base-dev`` does *not* pull in; the
  official Qt installer and the Windows binaries ship it as part of qtbase.
  Without it the application still builds and runs, but GeoPackage import is
  disabled and says so.
* Internet access at configure time: CMake FetchContent downloads
  `QXlsx <https://github.com/QtExcel/QXlsx>`_ (MIT). QCustomPlot (GPLv3),
  KissFFT (BSD) and `miniz <https://github.com/richgel999/miniz>`_ (MIT) are
  vendored in ``third_party/``.

On Debian/Ubuntu:

.. code-block:: bash

   sudo apt install cmake g++ qt6-base-dev qt6-base-dev-tools \
       qt6-base-private-dev libqt6sql6-sqlite libgl1-mesa-dev

Building
--------

.. code-block:: bash

   git clone https://github.com/sschwindt/ADV-Explorer.git
   cd ADV-Explorer
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build -j$(nproc)
   ./build/adv-explorer

On Windows, configure with ``-G "Visual Studio 17 2022" -A x64`` and build
with ``cmake --build build --config Release``.

Tests
-----

The QtTest suite verifies the readers, statistics, despiking, rotation
corrections, and project serialization. Several tests assert numeric parity
with the Python/Matlab predecessor
`tke-calculator <https://tke-calculator.readthedocs.io/>`_ (reference values
computed with numpy/pandas are hardcoded in ``tests/tst_core.cpp``; the sample
measurement lives in ``tests/data/``).

.. code-block:: bash

   ctest --test-dir build --output-on-failure     # all tests
   ./build/tests/tst_core flowStatsParity         # a single test function

Architecture
------------

* ``src/core/`` is a GUI-free static library (``advcore``): file readers
  (``VnaReader``, ``CsvReader``, ``FlowTrackerReader`` and
  ``FlowTrackerCsvReader``) reached through ``FormatRegistry``, the ZIP
  wrapper ``ZipArchive``, statistics (``FlowStats``) and their mode-dependent
  labels (``StatsLabels``), despiking filters (``Despike``), probe alignment
  (``Rotation``), map projections (``Crs``), surveyed-position import
  (``GeoPointImport``), the measurement point model (``MeasurementPoint``,
  ``ProjectModel``, ``ProjectSettings``), project serialization (``Project``),
  and xlsx export (``ProfileStatsExport``).
* ``src/gui/`` contains the Qt Widgets front end: ``MainWindow``, the
  ``SiteView`` base with its two implementations ``FlumeView`` (laboratory)
  and ``MapView`` (field, OpenStreetMap tiles), ``PointWizard``,
  ``ImportWizard``, ``FlowTrackerImportWizard``, ``CrsDialog``, ``PlotFrame``
  (QCustomPlot time series), ``ProfileFrame`` (vertical profiles and
  alignment), and dialogs.
* ``tools/make_template.py`` regenerates ``templates/ADV-profiles.xlsx``;
  its column order must match
  ``statsexport::profileTemplateColumns()``, which is therefore deliberately
  independent of the campaign mode.

Things that are easy to break
-----------------------------

* ``FlumeView`` scales its two axes independently so the channel fills the
  panel. ``MapView`` must never do that: it is conformal by construction, and
  shearing it would misplace every point against the basemap.
* The derived TKE series is persisted under the stable identifier ``"@tke"``,
  never under its display name, because that name depends on the mode. A
  project saved in one mode would otherwise lose the curve in the other.
* FlowTracker2 import computes each station position once and gives it to all
  points of that vertical. Recomputing per point can differ in the last bits,
  and profiles are keyed on coordinates formatted to four decimals, so one
  vertical would split into several.
* ``QFileInfo("x.ft.dat.csv").suffix()`` is ``"csv"``. Format dispatch must go
  through ``formats::byFilePath()``, which matches the longest file-name
  ending rather than the suffix.
* ``tests/data`` holds binary fixtures (a ZIP and a GeoPackage). ``.gitattributes``
  marks them binary; end-of-line conversion on a Windows checkout would corrupt
  them silently.

Releases and continuous integration
-----------------------------------

``.github/workflows/build.yml`` builds and tests on every push:

* the Linux job (Ubuntu 22.04, so the result runs on Debian 12+ class
  systems) packages a self-contained AppImage with linuxdeploy,
* the Windows job (MSVC, Qt via aqtinstall) packages a portable zip with
  windeployqt.

Both packaging steps then check that the result actually carries what field
mode needs at runtime: the SQLite driver plugin for GeoPackage import and a
TLS backend for the map tiles. Qt uses Schannel on Windows, while the AppImage
additionally bundles OpenSSL, without which every tile request would fail
silently.

Pushing a tag ``v*`` attaches both packages to a GitHub release.

The Windows executables are not code-signed, so Windows SmartScreen shows
its *unrecognized app* warning on first launch (see :doc:`installation`).

Documentation screenshots
-------------------------

The screenshots on these pages are generated by the application itself so
they stay current:

.. code-block:: bash

   QT_QPA_PLATFORM=offscreen ./build/adv-explorer --screenshots docs/img

This documentation is built by `Read the Docs <https://readthedocs.org/>`_
from the ``docs/`` folder (Sphinx, configuration in ``.readthedocs.yaml``).
