# ADV-Explorer

Interactive desktop application for analyzing acoustic Doppler velocimetry (ADV)
measurements, such as those recorded with a Nortek Vectrino or UBERTONE profilers.
ADV-Explorer visualizes velocity time series, filters spikes, computes turbulence
statistics, corrects probe misalignment, and exports plots, data, and statistics.

**Documentation**: https://adv-explorer.readthedocs.io (motivation, installation,
usage with screenshots, developer guide)

## Features

- Interactive flume top view (1:5 aspect ratio) with the coordinate origin at the
  center of the inlet: x streamwise, y transverse (orographic left bank negative),
  z upward from the flume bottom. Click into the flume to place measurement points.
- One circle marker per z position; markers turn dark blue once a water depth is
  assigned (setting the depth at one x-y position applies to all points there).
- Time-series plots of any data column (u, v, w1, w2, amplitudes, SNR, correlations,
  instantaneous TKE) with color-blind friendly palettes (Okabe-Ito, Paul Tol),
  configurable line styles (solid, dotted, dashed, dash-dot) and markers
  (rectangular, circular, triangular, filled/open); superposition of multiple
  points; optional second plot frame.
- Despiking filters, chainable per point:
  - correlation score threshold (default 70)
  - signal-to-noise ratio threshold (default 20)
  - velocity threshold |u - mean| > k std
  - Goring and Nikora (2002) velocity/acceleration thresholding
  - iterative phase-space thresholding (PST)
  - gap replacement: NaN or linear interpolation
- Vertical profile view (z or z/h axis) of mean U, V, W with a statistics panel:
  mean, standard deviation, skewness, kurtosis, Reynolds stresses (u'v', u'w', v'w'),
  TKE, and dissipation rate (inertial-subrange spectral fit).
- Heading/pitch/roll probe-alignment correction per x-y profile with automatically
  proposed angles (zeroing mean V, mean W, and residual v'w') and manual override.
- Exports: PNG at 300 dpi exactly as displayed, CSV of all shown series, per-point
  statistics workbook, and vertical profile statistics written into
  `templates/ADV-profiles.xlsx` (with velocity magnitude and direction formulas).
- Standalone `.advProj` project files that embed all measurement data and settings,
  so projects open on any computer without the original data paths.

## Requirements

### Running (no build required)

Download the executables from the
[releases page](https://github.com/sschwindt/ADV-Explorer/releases):

- **Linux** (Debian 12+, Ubuntu 22.04+, Linux Mint 21+, x86_64):
  download `adv-explorer-linux-x86_64.AppImage`, make it executable, and run it.
  All Qt libraries are bundled.

  ```bash
  chmod +x adv-explorer-linux-x86_64.AppImage
  ./adv-explorer-linux-x86_64.AppImage
  ```

- **Windows 10/11** (x86_64): download `adv-explorer-windows-x86_64.zip`, unzip
  it, and double-click `adv-explorer.exe`. All Qt DLLs are included.

### Building from source

- CMake >= 3.16 and a C++17 compiler (GCC >= 10, MSVC >= 2019)
- Qt 6 development packages (Widgets, Concurrent, PrintSupport, Test, private headers):
  `sudo apt install qt6-base-dev qt6-base-dev-tools qt6-base-private-dev libgl1-mesa-dev`
  on Debian/Ubuntu, or the [Qt online installer](https://www.qt.io/download) on Windows
- Internet access at configure time (CMake FetchContent downloads
  [QXlsx](https://github.com/QtExcel/QXlsx)); QCustomPlot and KissFFT are vendored
  in `third_party/`

## Installation

### Linux

```bash
git clone https://github.com/sschwindt/ADV-Explorer.git
cd ADV-Explorer
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build          # optional: run unit tests
./build/adv-explorer
```

### Windows

```bat
git clone https://github.com/sschwindt/ADV-Explorer.git
cd ADV-Explorer
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\adv-explorer.exe
```

## Usage

1. **Add measurement points**: click into the flume view and complete the wizard
   (x-y-z position in meters with 4-digit precision, data file, optional water
   depth, analysis time window, despiking filters). Alternatively use
   *Import > Import ADV files...* to add many files at once; coordinates are
   pre-filled from `XX_YY_ZZ_*.vna` style names (centimeters, `__` prefix for
   negative values).
2. **Inspect time series**: in the *Time series* tab, choose a point and data
   column, press *Add*, and style each series (line, markers, palette). Use
   *View > Add plot frame below* for a second stacked plot frame.
3. **Vertical profiles**: in the *Vertical profiles* tab, select an x-y profile,
   toggle U/V/W and z vs z/h, and read the statistics panel. Use
   *Probe alignment...* to apply the proposed (or manual) heading/pitch/roll
   correction of that profile.
4. **Export**: *Export > Data* writes CSV or statistics workbooks;
   *Export > Plots* saves the current frame as a 300 dpi PNG.
5. **Save your work**: *File > Save project* writes a standalone `.advProj` file
   including all data.

### Supported input formats

- **`.vna`** Nortek Vectrino ASCII exports (20 whitespace-separated columns:
  time, sample, u, v, w1, w2, amplitudes, SNR, correlations)
- **Delimited text** (`.csv`, `.txt`, `.dat`, e.g., UBERTONE exports): the import
  wizard shows a column-mapping table to assign time and velocity components;
  delimiter (comma, semicolon, tab, whitespace) and decimal commas are detected
  automatically.

## Predecessor code

The numerical methods (Goring and Nikora despiking, flow statistics, TKE) were
ported from the Python/Matlab [tke-calculator](https://tke-calculator.readthedocs.io/),
included in `tke-calculator-main/` for reference and used by the unit tests.

## License

ADV-Explorer is released under the GNU General Public License v3 (see `LICENSE`).
It bundles [QCustomPlot](https://www.qcustomplot.com/) (GPLv3),
[KissFFT](https://github.com/mborgerding/kissfft) (BSD-3-Clause), and fetches
[QXlsx](https://github.com/QtExcel/QXlsx) (MIT) at build time.
