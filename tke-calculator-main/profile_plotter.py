"""Plot functions for TKE visualization

Note:
    The script represents merely a start for plotting normalized TKE against normalized X. If required, enrich this
    script with more plot functions and integrate them in profile_analyst.process_vna_files at the bottom of the function.
"""
import matplotlib.pyplot as _plt
import matplotlib.font_manager as _font_manager
import numpy as _np


def plot_xy(x, y, file_name):
    """
    Plots y data against x (1d-numpy array) and markers of local maxima and minima

    Args:
        x (numpy.array): x data
        y (numpy.array): y data

    Returns:
        show and save plot in test folder as norm-TKE-x.png
    """

    # set font properties
    hfont = {'family': 'normal',
             'weight': 'normal',
             'size': 10,
             'style': 'normal',
             'fontname': 'Arial'}
    font = _font_manager.FontProperties(family=hfont['fontname'],
                                       weight=hfont['weight'],
                                       style=hfont['style'],
                                       size=hfont['size'])
    # create plot
    fig = _plt.figure(figsize=(6, 3), dpi=220, facecolor='w', edgecolor='k')
    axe = fig.add_subplot(1, 1, 1)
    axe.scatter(x, y, color="slategray", label="normarlized TKE", s=10, edgecolor="black")

    # Define axis labels and legend
    axe.set_xlabel("x / wood D (-)", **hfont)
    axe.set_ylabel(r"TKE / U$^{2}$ (-)", **hfont)
    # axe.legend(loc='upper left', prop=font, facecolor='w', edgecolor='gray', framealpha=1, fancybox=0)

    # Set grid
    axe.grid(color='gray', linestyle='-', linewidth=0.2)
    # axe.set_ylim((0, int(_np.nanmax(y) * 1.1)))
    # axe.set_xlim((int(_np.nanmin(x)) * 1.1, int(_np.nanmax(x) * 1.1)))

    # Output plot
    _plt.savefig(file_name, bbox_inches='tight')
