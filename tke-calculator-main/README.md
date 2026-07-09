# tke-calculator

Read the detailed Python code docs at https://tke-calculator.readthedocs.io/


## Requirements

Python `>=3.6`
NumPy `>=1.20`
Pandas `>=1.4.1`

## Usage

* Adapt the input parameters in `input.xlsx`
* Run the main script `python profile_analyst.py`

## Comparison of Matlab and Python scripts

The Matlab codes correspond to the following Python files.

| Matlab file | Python file                |
|-------------|----------------------------|
| main_LP.m   | profile_analyst.py         |
| flowstat.m  | flowstat.py                |
| rmspike.m   | rmspike.py                 |

In addition, the following Python files were added:

* `config.py` contains global variables
* `profile_plotter.py` contains plot functions that were originally in `main_LP.m`
