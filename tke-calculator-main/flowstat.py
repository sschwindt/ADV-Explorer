import numpy as np
import logging


def flowstat(time, u, v, w1, w2, profile_type="lp"):
    """Calculate ADV data statistics

    Args:
        time (np.array): time in seconds
        u (np.array): streamweise velocity along x-axis (positive in bulk flow direction)
        v (np.array): perpendicular velocity along y-axis
        w1 (np.array): vertical velocity if side is DOWN
        w2 (np.array): vertical velocity if side is not DOWN
        profile_type (str): orientation of the probe (default: lp, which mean probe looks like FlowTracker in a river)

    Returns:
        time_series (dict): keys correspond to series names and values to full time series
        stats (dict(dict)): keys correspond to series names with STAT for autoreplacement with STAT type of nested dictionaries with AVRG, STD and STDERR
    """
    if profile_type == "lp":
        w = w1
    else:
        w = w2
    # flow statistics: velocity (m/s), mean (m/s), rms (m/s), stderr (m/s)
    u_stats = {
              "average": np.nanmean(u),
              "std": np.nanstd(u),
              "stderr": np.nanstd(u) / np.sqrt(np.count_nonzero(~np.isnan(u)))
    }

    v_stats = {
              "average": np.nanmean(v),
              "std": np.nanstd(v),
              "stderr": np.nanstd(v) / np.sqrt(np.count_nonzero(~np.isnan(v)))
    }

    w_stats = {
              "average": np.nanmean(w),
              "std": np.nanstd(w),
              "stderr": np.nanstd(w) / np.sqrt(np.count_nonzero(~np.isnan(w)))
    }

    # calculate Reynolds stress for u and v
    tau_re0 = np.multiply(u - u_stats["average"], v - v_stats["average"])
    tau_re0_stats = {
                    "average": np.nanmean(tau_re0),
                    "std": np.nanstd(tau_re0),
                    "stderr": np.nanstd(tau_re0) / np.sqrt(np.count_nonzero(~np.isnan(tau_re0)))
    }

    # calculate Reynolds stress for u and w1
    tau_re1 = np.multiply(u - u_stats["average"], w - w_stats["average"])
    tau_re1_stats = {
                    "average": np.nanmean(tau_re1),
                    "std": np.nanstd(tau_re1),
                    "stderr": np.nanstd(tau_re1) / np.sqrt(np.count_nonzero(~np.isnan(tau_re1)))
    }

    # calculate TKE k_t1 in m^2/s^2
    k_t1 = 0.5 * (u_stats["std"] ** 2 + v_stats["std"] ** 2 + w_stats["std"] ** 2)

    # prepare output data
    time_series = {"t (s)": time, "u (m/s)": u, "v (m/s)": v, "w1 (m/s)": w1,
                   "tau_v (m^2/s^2)": tau_re0, "tau_w (m^2/s^2)": tau_re1}
    stats = {"u STAT (m/s)": u_stats, "v STAT (m/s)": v_stats, "w STAT (m/s)": w_stats,
             "TKE (m^2/s^2)": k_t1, "tau_v STAT (m^2/s^2)": tau_re0_stats, "tau_w STAT (m^2/s^2)": tau_re1_stats}

    # if downward looking: calculate stats, TKE, and Reynolds stress for u and w2
    if not (profile_type == "lp"):
        w2_stats = {
            "average": np.nanmean(w2),
            "std": np.nanstd(w2),
            "stderr": np.nanstd(w2) / np.sqrt(np.count_nonzero(~np.isnan(w2)))
        }
        # calculate TKE k_t1 in m^2/s^2
        k_t2 = 0.5 * (u_stats["std"] ** 2 + v_stats["std"] ** 2 + w2_stats["std"] ** 2)
        tau_re2 = np.multiply(u - u_stats["average"], w2 - w2_stats["average"])
        tau_re2_stats = {
            "average": np.nanmean(tau_re2),
            "std": np.nanstd(tau_re2),
            "stderr": np.nanstd(tau_re2) / np.sqrt(np.count_nonzero(~np.isnan(tau_re2)))
        }
    else:
        # build empty data structures for longitudinal profiling
        w2_stats = {
            "average": np.nan,
            "std": np.nan,
            "stderr": np.nan,
        }
        tau_re2 = {"nan": np.nan}
        k_t2 = {"nan": np.nan}
        tau_re2_stats = {"nan": np.nan}
    # update output data
    time_series.update({"tau_w2 (m^2/s^2)": tau_re2})
    stats.update({"TKE2 (m^2/s^2)": k_t2, "tau_w2 STAT (m^2/s^2)": tau_re2_stats, "w2 STAT (m/s)": w2_stats})

    return time_series, stats
