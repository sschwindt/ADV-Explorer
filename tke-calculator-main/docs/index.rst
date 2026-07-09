.. tke-analyst documentation parent file.


About
=====

This Python3 code aids in analyzing raw measurements with an Acoustic Doppler Velocimeter (ADV) producing ``*.vno`` and ``*.vna`` files. It detects and removes spikes according to `Nikora and Goring (1998) <https://doi.org/10.1061/(ASCE)0733-9429(1998)124:6(630)>`_ and  `Goring and Nikora (2002) <https://doi.org/10.1061/(ASCE)0733-9429(2002)128:1(117)>`_.

The code was originally developed in Matlab(R) at the `Nepf Environmental Fluid Mechanics Laboratory <https://nepf.mit.edu/>`_ (`Massachusetts Institute of Technology <https://web.mit.edu/>`_).

.. important::

    ``*.vno`` and ``*.vna`` files need to comply with the following name convention:
    ``XX_YY_ZZ_something.vna`` where ``XX``, ``YY``, and ``ZZ`` are streamwise (x), perpendicular (y), and vertical (z) coordinates in CENTIMETERS, respectively. Anything else added after ``ZZ_`` is ignored by the code (it just copies it for the sake of dataset naming).

.. note::

    This documentation is also as available as `style-adapted PDF <https://tke-calculator.readthedocs.io/_/downloads/en/latest/pdf/>`_.

Requirements \& Installation
============================

*Time requirement: 5-10 min.*

Get Python
----------

To get the code running, the following software is needed and their installation instructions are provided below:

- Python `>=3.6`
- NumPy `>=1.17.4`
- Openpyxl `3.0.3`
- Pandas `>=1.3.5`
- Matplotlib `>=3.1.2`

Start with downloading and installing the latest version of `Anaconda Python <https://www.anaconda.com/products/individual>`_.  Alternatively, downloading and installing a pure `Python <https://www.python.org/downloads/>`_ interpreter will also work. Detailed information about installing Python is available in the `Anaconda Docs <https://docs.continuum.io/anaconda/install/windows/>`_ and at `hydro-informatics.com/python-basics <https://hydro-informatics.com/python-basics/pyinstall.html>`_.

To install the NumPy, Openpyxl, Pandas, and Matplotlib libraries after installing Anaconda, open Anaconda Prompt (e.g., click on the Windows icon, tap ``anaconda prompt``, and hit enter``). In Anaconda Prompt, enter the following command sequence to install the libraries in the **base** environment. The installation may take a while depending on your internet speed.

.. code-block::

    conda install -c anaconda numpy
    conda install -c anaconda openpyxl
    conda install -c anaconda numpy
    conda install -c conda-forge pandas
    conda install -c conda-forge matplotlib

If you are struggling with the dark window and blinking cursor of Anaconda Prompt, worry not. You can also use Anaconda Navigator and install the four libraries (in the above order) in Anaconda Navigator.

.. note::

    Alternatively, create a new conda environment to install the three libraries for this application. However, creating a new environment may eat up a lot of disk space, and installing the Python-omnipresent libraries NumPy, Openpyxl, Pandas, and Maplotlib in the **base** environment does not hurt.

Download tke-analyst Code
-------------------------

The code can be either started from Terminal (Anaconda Prompt) or within an Integrated Development Environment (**IDE**). With Anaconda installed, consider using Spyder (Anaconda Navigator > `Spyder IDE <https://www.spyder-ide.org/>`_).

`Download tke-calculator.zip <https://github.com/sschwindt/tke-calculator/archive/refs/heads/main.zip>`_  and unpack it to the directory where you want to run the code.

.. tip::

    Alternatively to downloading the zip file, you may want to ``git clone`` the repository, which enables regular updating of the code (e.g., if there is an update of plot functions available). For using git, make sure that `git bash <https://git-scm.com/downloads>`_ is installed on your computer. Then, open git bash, `cd <https://docs.microsoft.com/en-us/windows-server/administration/windows-commands/cd>`_ into the directory where you want to download the code and type:

    ``git clone https://github.com/sschwindt/tke-calculator.git``

    To update any time, ``cd`` into the directory where ``tke-calculator`` lives and type:

    ``git pull --rebase``


.. toctree::
    :hidden:
    :maxdepth: 2

    TKE Analyst <self>

.. toctree::
    :hidden:

    Usage <usage>

.. toctree::
    :hidden:

    Developer Docs <codedocs>

.. toctree::
    :hidden:

    License <license>


