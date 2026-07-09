import numpy as np
import pandas as pd
import logging


def rmspike(vna_df, u_stats, v_stats, w_stats, w2_stats=None,
			method="velocity",
			freq=200., lambda_a=1.0, k=3.0, profile_type="lp"):
	""" Spike removal and replacement - see Nikora & Goring (1999) and Goring & Nikora (2002).

	Args:
		vna_df (pandas.DataFrame): matrix-like data array of the vna measurement file
		u_stats (pandas.DataFrame): streamwise velocity stats from flowstat function
		v_stats (pandas.DataFrame): perpendicular velocity stats from flowstat function
		w_stats (pandas.DataFrame): vertical velocity stats from flowstat function
		w2_stats (pandas.DataFrame): sec. vertical velocity stats from flowstat function (only required if profile_type is not lp)
		method (str): determines whether to use acceleration or velocity (default) for despiking
		freq (int): sampling frequency in 1/s (Hz); default is 200 Hz
		lambda_a (float): multiplier of gravitational acceleration (acceleration threshold)
		k (float): multiplier of velocity stdev (velocity threshold)
		side (str): orientation of the probe (default: DOWN, which mean probe looks like FlowTracker in a river)

	Note:
		Goring & Nikora (2002) suggest lambda_a = 1.0 ~ 1.5 and k = 1.5, but we shall use lambda_a = 1.0 and k = 3 ~ 9.
		SonTek, Nortek, and Lei recommend the SNR and correlation thresholds to be 15 and 70 respectively.
		Though data points have high SNR, the correlation can be low.
	"""

	# Acceleration a
	# Any operation with NaN will return NaN
	# Hence, only two consecutive data records (i.e. delta t = 1/f) will produce a set of acceleration components.
	# shift(1) makes the first (0) entries of a_i being np.nan (i.e. first acceleration is in 2nd row of a_i)
	print("   - calculating accelerations ...")
	a_x = (vna_df["u (m/s)"].shift(-1) - vna_df["u (m/s)"]).shift(1) * freq
	a_y = (vna_df["v (m/s)"].shift(-1) - vna_df["v (m/s)"]).shift(1) * freq
	a_z = (vna_df["w1 (m/s)"].shift(-1) - vna_df["w1 (m/s)"]).shift(1) * freq
	if not(profile_type == "lp"):
		a_z2 = (vna_df["w2 (m/s)"].shift(-1) - vna_df["w2 (m/s)"]).shift(1) * freq
		# set headers as a function of probe orientation
		hdr_spkct = ["u spikes", "v spikes", "w1 spikes", "w2 spikes"]
	else:
		# set headers as a function of probe orientation
		hdr_spkct = ["u spikes", "v spikes", "w spikes"]

	# Goring & Nikora (2002)'s Acceleration Thresholding Method (ATM) tests the acceleration thresholds first
	print("   - looking for acceleration spikes ...")
	a_cr = 9.81 * lambda_a  # acceleration threshold
	spikeu = np.nansum(np.where(~((a_x > -a_cr) & (a_x < a_cr)) & ~np.isnan(a_x), 1, 0))
	spikev = np.nansum(np.where(~((a_y > -a_cr) & (a_y < a_cr)) & ~np.isnan(a_y), 1, 0))
	spikew1 = np.nansum(np.where(~((a_z > -a_cr) & (a_z < a_cr)) & ~np.isnan(a_z), 1, 0))
	spikevct = [spikeu, spikev, spikew1]
	if not (profile_type == "lp"):
		spikew2 = np.nansum(np.where(~((a_z2 > -a_cr) & (a_z2 < a_cr)) & ~np.isnan(a_z2), 1, 0))
		spikevct.append(spikew2)

	# calculate velocity thresholds for despiking
	print("   - looking for velocity spikes ...")
	u_crl = u_stats["average"] - k * u_stats["std"]
	u_cru = u_stats["average"] + k * u_stats["std"]
	v_crl = v_stats["average"] - k * v_stats["std"]
	v_cru = v_stats["average"] + k * v_stats["std"]
	w_crl = w_stats["average"] - k * w_stats["std"]
	w_cru = w_stats["average"] + k * w_stats["std"]

	spikeu_vel = np.nansum(np.where(~((vna_df["u (m/s)"] > u_crl) & (vna_df["u (m/s)"] < u_cru)) & ~np.isnan(vna_df["u (m/s)"]), 1, 0))
	spikev_vel = np.nansum(np.where(~((vna_df["v (m/s)"] > v_crl) & (vna_df["v (m/s)"] < v_cru)) & ~np.isnan(vna_df["v (m/s)"]), 1, 0))
	spikew1_vel = np.nansum(np.where(~((vna_df["w1 (m/s)"] > w_crl) & (vna_df["w1 (m/s)"] < w_cru)) & ~np.isnan(vna_df["w1 (m/s)"]), 1, 0))
	spikevct_vel = [spikeu_vel, spikev_vel, spikew1_vel]
	if not (profile_type == "lp"):
		w2_crl = w2_stats["average"] - k * w2_stats["std"]
		w2_cru = w2_stats["average"] + k * w2_stats["std"]
		spikew2_vel = np.nansum(np.where(~((vna_df["w2 (m/s)"] > w2_crl) & (vna_df["w2 (m/s)"] < w2_cru)) & ~np.isnan(vna_df["w2 (m/s)"]), 1, 0))
		spikevct_vel.append(spikew2_vel)

	if "vel" in method:
		print("   - running select VELOCITY despiking method (provided argument: %s)" % str(method))
		vna_df["u (m/s)"] = np.where(
			~((vna_df["u (m/s)"] > u_crl) & (vna_df["u (m/s)"] < u_cru)) & ~np.isnan(vna_df["u (m/s)"]),
			np.nan, vna_df["u (m/s)"]
			)
		vna_df["v (m/s)"] = np.where(
			~((vna_df["v (m/s)"] > v_crl) & (vna_df["v (m/s)"] < v_cru)) & ~np.isnan(vna_df["v (m/s)"]),
			np.nan, vna_df["v (m/s)"]
		)
		vna_df["w1 (m/s)"] = np.where(
			~((vna_df["w1 (m/s)"] > w_crl) & (vna_df["w1 (m/s)"] < w_cru)) & ~np.isnan(vna_df["w1 (m/s)"]),
			np.nan, vna_df["w1 (m/s)"]
		)
		if not (profile_type == "lp"):
			vna_df["w2 (m/s)"] = np.where(
				~((vna_df["w2 (m/s)"] > w2_crl) & (vna_df["w2 (m/s)"] < w2_cru)) & ~np.isnan(vna_df["w2 (m/s)"]),
				np.nan, vna_df["w2 (m/s)"]
			)
	else:
		print("   - running select ACCELERATION despiking method (provided argument: %s)" % str(method))
		vna_df["u (m/s)"] = np.where(
			~((a_x > -a_cr) & (a_x < a_cr)) & ~np.isnan(a_x),
			np.nan, vna_df["u (m/s)"]
		)
		vna_df["v (m/s)"] = np.where(
			~((a_y > -a_cr) & (a_y < a_cr)) & ~np.isnan(a_y),
			np.nan, vna_df["v (m/s)"]
		)
		vna_df["w1 (m/s)"] = np.where(
			~((a_z > -a_cr) & (a_z < a_cr)) & ~np.isnan(a_z),
			np.nan, vna_df["w1 (m/s)"]
		)
		if not (profile_type == "lp"):
			vna_df["w2 (m/s)"] = np.where(
				~((a_z2 > -a_cr) & (a_z2 < a_cr)) & ~np.isnan(a_z2),
				np.nan, vna_df["w2 (m/s)"]
			)

	# create output documentation dataframe
	spike_stats = pd.DataFrame(data=[spikevct, spikevct_vel],
								columns=hdr_spkct,
								index=["acceleration", "velocity"])

	return spike_stats, vna_df

