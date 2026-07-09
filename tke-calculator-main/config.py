"""Global parameters settings (essentially SCRIPT_DIR) and message logging controls."""
import os as _os
import sys
import logging


SCRIPT_DIR = _os.path.abspath("") + "/"
NAN = -9999.0

PROFILE_KEYS = {
    "longitudinal": "lp",
    "down": "down",
}

# define data headers
HEADERS = ["File #", "x (m)", "y (m)", "z (m)",
           "u_avr (m/s)", "u_stderr (m/s)", "u_rms (m/s)",
           "v_avr (m/s)", "v_stderr (m/s)", "v_rms (m/s)",
           "w_avr (m/s)", "w_stderr (m/s)", "w_rms (m/s)",
           "k_t (m^2/s^2)", "tau_avr_w (m^2/s^2)", "tau_stderr_w (m^2/s^2)",
           "tau_avr_v (m^2/s^2)", "tau_stderr_v (m^2/s^2)"]

# silence openpyxl warnings when reading input.xlsx
import warnings
warnings.filterwarnings("ignore", category=UserWarning, module="openpyxl")
warnings.filterwarnings("ignore", category=UserWarning, module="matplotlib")


def log_actions(fun):
    def wrapper(*args, **kwargs):
        start_logging()
        fun(*args, **kwargs)
        logging.shutdown()
    return wrapper


def start_logging():
    # logging.root.handlers = []
    logging.basicConfig(filename="logfile.log",
                        format="[%(asctime)s] %(message)s",
                        filemode="w", level=logging.WARNING,
                        )
    logging.getLogger().addHandler(logging.StreamHandler(sys.stdout))
    # disable font warnings from matplotlib
    logging.getLogger("matplotlib.font_manager").disabled = True
