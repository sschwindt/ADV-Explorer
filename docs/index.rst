ADV-Explorer
============

ADV-Explorer is a free desktop application for analyzing acoustic Doppler
velocimetry (ADV) measurements, such as those recorded with a Nortek Vectrino
or UBERTONE profilers in laboratory flumes.

.. figure:: img/main-window.png
   :alt: The ADV-Explorer main window with flume view and time series plot

   *The ADV-Explorer main window: interactive flume top view (top) and
   time series plot frame (bottom).*

Motivation
----------

Flume experiments with ADV probes produce large numbers of point measurements
that need quality control before any turbulence statistic can be trusted:
spikes from air bubbles or weak acoustic seeding must be filtered, probe
misalignment must be corrected, and every measurement position must be tracked
in a consistent coordinate system. Doing this with one-off scripts is slow and
error prone, and it makes results hard to reproduce or share.

ADV-Explorer replaces such scripts with an interactive workflow:

* Measurement points are placed on a top view of the flume with a consistent
  coordinate reference system (origin at the center of the inlet, x pointing
  downstream, y toward the right bank, z upward from the bottom).
* Velocity time series are inspected visually and cleaned with established
  despiking filters (correlation and signal-to-noise thresholds, velocity
  thresholding, Goring and Nikora 2002 methods, phase-space thresholding).
* Turbulence statistics (mean, standard deviation, skewness, kurtosis,
  Reynolds stresses, turbulent kinetic energy, and dissipation rate) are
  computed per point and per vertical profile.
* Probe misalignment is corrected per vertical profile with proposed or
  manual heading, pitch, and roll rotations.
* An entire analysis, including all raw data, lives in one standalone
  ``.advProj`` file that opens on any computer.

The numerical methods were ported from the Python/Matlab
`tke-calculator <https://tke-calculator.readthedocs.io/>`_ and are verified
against it by unit tests.

Contents
--------

.. toctree::
   :maxdepth: 2

   installation
   usage
   developer

License
-------

ADV-Explorer is released under the GNU General Public License v3. The source
code is available at `github.com/sschwindt/ADV-Explorer
<https://github.com/sschwindt/ADV-Explorer>`_.
