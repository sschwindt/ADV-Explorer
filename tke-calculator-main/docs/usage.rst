
Usage
=====

Regular Usage
-------------

With Python installed and the code living on your computer:

- Copy your data to a sub-folder of ``tke-analyst`` (e.g., next to the folder ``data/test-example`` that contains three exemplary ``*.vna`` files). Make sure the files are named with ``XX_YY_ZZ_something.vna`` where ``XX``, ``YY``, and ``ZZ`` are streamwise (x), perpendicular (y), and vertical (z) coordinates in CENTIMETERS, respectively
- Complete the required information on the experimental setup in ``tke-calculator/input.xlsx`` (see below figure). **IMPORTANT: Never modify column A or any list in the sourcetables sheet (unless you also modify** ``load_input_defs`` **in line 25ff of** ``profile_analyst.py`` **).** The code uses the text provided in these areas of *input.xlsx* to identify setups. If useful, consider substituting the *Wood* wording in your mind and with a note in column C with your characteristic turbulence objects, but do not modify column A.
- Open Anaconda Prompt (or any other Python-able Terminal) and:
    + ``cd`` into the code directory (e.g., ``cd "C:research\project\tke-analyst"`` if you unpacked ``tke-analyst`` to a folder living in the directory ``C:\research\project\``)
    + run the code: ``python profile_analyst.py`` (uses ``input.xlsx``)
    + ALTERNATIVELY, run with another ``*.xlsx`` input file: ``python profile_analyst.py "input-other-test.xlsx"``
    + wait until the code finished with ``-- DONE -- ALL TASKS FINISHED --``

.. figure:: https://github.com/sschwindt/tke-calculator/raw/main/docs/img/input-xlsx.jpg
   :alt: input turbulent tke experiment setup parameters

   *The interface of the input.xlsx workbook for entering experiment parameters and specifying a despiking method.*


- After a successful run, the code will have produced the following files in ``...\tke-analyst\TEST`` (where ``TEST`` may correspond to ``test-example``):
    + ``.xlsx`` files of full-time series data, with spikes and despiked.
    + ``.xlsx`` files of statistic summaries (i.e., average, standard deviation *std*, TKE) of velocity parameters with x, y, and z positions, with spikes and despiked (see workbook example in the figure below).
    + Two plots (``norm-tke-x.png`` and ``norm-tke-x-despiked.png``) showing normalized TKE plotted against normalized x, with spikes and despiked, respectively (see plot example in the figure below).

.. figure:: https://github.com/sschwindt/tke-calculator/raw/main/docs/img/output-example.jpg
   :alt: example output tke-calculator

    *Exemplary output workbook of despiked statistics, such as averages, standard deviations, and standard errors of u, v, w, shear stresses (tau) and TKE.*

.. figure:: https://github.com/sschwindt/tke-calculator/raw/main/docs/img/norm-tke-x-despiked.png
   :alt: example output normalized tke plot

    *Exemplary outputof normalized TKE vs. normalized x coordinates.*


Usage Example
-------------

For example, download and unpack the code to your hard-disk in a folder called ``C:\my-project\tke-analyst\``. To analyze the ``*.vna`` files in ``test-example``, they were copied into a test folder that lives in the ``data`` folder.

The definitions in the above-shown ``input.xlsx`` define x-normalization as a function of a wood log length, in this case, the log diameter of 0.114 m.

Cell ``B3`` containing **Input folder name (tke-analyst/)** in ``input.xlsx`` defines that the input data for ``test-example`` live in a subfolder called  ``data/test-example``.

.. important::

    The data directory of the subfolder definition in cell ``B3`` may not end on any ``\`` or  ``/`` . Also, make sure to **use the** ``/`` **sign for folder name separation** (do not use ``\``).

To run the code with the example data, open Anaconda Prompt (or any other Python-able Terminal) and:
    + ``cd`` into the code directory (e.g., ``cd "C:research\project\tke-analyst"`` if you unpacked ``tke-analyst`` to a folder living in the directory *C:research\project\*)
    + run the code: ``python profile_analyst.py`` (uses ``input.xlsx``)
    + Or: ``python profile_analyst.py "input.xlsx"``
    + wait until the code finished with ``-- DONE -- ALL TASKS FINISHED --``
- After a successful run, the code will have produced the following files in ``...\tke-analyst\data\test-example``:
    + ``.xlsx`` files of full-time series data, with spikes and despiked.
    + ``.xlsx`` files of statistic summaries (i.e., average, standard deviation *std*, TKE) of velocity parameters with x, y, and z positions, with spikes and despiked.
    + Two plots (``norm-tke-x.png`` and ``norm-tke-x-despiked.png``) showing normalized TKE plotted against normalized x, with spikes and despiked, respectively.







