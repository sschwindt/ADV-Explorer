/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "FlowStats.h"
#include "AdvData.h"

#include <kiss_fftr.h>

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace adv {
namespace flowstats {

SeriesStats seriesStats(const QVector<double> &values)
{
    SeriesStats s;
    double sum = 0.0;
    for (double v : values) {
        if (std::isfinite(v)) {
            sum += v;
            ++s.n;
        }
    }
    if (s.n == 0) {
        s.mean = s.std = s.stderror = s.skewness = s.kurtosis = nan();
        return s;
    }
    s.mean = sum / s.n;

    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    for (double v : values) {
        if (!std::isfinite(v))
            continue;
        const double d = v - s.mean;
        m2 += d * d;
        m3 += d * d * d;
        m4 += d * d * d * d;
    }
    m2 /= s.n;
    m3 /= s.n;
    m4 /= s.n;
    s.std = std::sqrt(m2);
    s.stderror = s.std / std::sqrt(double(s.n));
    s.skewness = (m2 > 0.0) ? m3 / std::pow(m2, 1.5) : nan();
    s.kurtosis = (m2 > 0.0) ? m4 / (m2 * m2) - 3.0 : nan();
    return s;
}

double nanMean(const QVector<double> &values)
{
    double sum = 0.0;
    int n = 0;
    for (double v : values) {
        if (std::isfinite(v)) {
            sum += v;
            ++n;
        }
    }
    return n > 0 ? sum / n : nan();
}

double crossStress(const QVector<double> &a, const QVector<double> &b)
{
    const int n = std::min(a.size(), b.size());
    if (n == 0)
        return nan();
    const double meanA = nanMean(a);
    const double meanB = nanMean(b);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (std::isfinite(a[i]) && std::isfinite(b[i])) {
            sum += (a[i] - meanA) * (b[i] - meanB);
            ++count;
        }
    }
    return count > 0 ? sum / count : nan();
}

PointStats compute(const QVector<double> &u, const QVector<double> &v,
                   const QVector<double> &w, double samplingFrequency)
{
    PointStats p;
    p.u = seriesStats(u);
    p.v = seriesStats(v);
    p.w = seriesStats(w);
    p.uv = crossStress(u, v);
    p.uw = crossStress(u, w);
    p.vw = crossStress(v, w);
    p.tke = 0.5 * (p.u.std * p.u.std + p.v.std * p.v.std + p.w.std * p.w.std);
    p.eps = dissipationRate(u, samplingFrequency);
    p.magnitude = std::sqrt(p.u.mean * p.u.mean + p.v.mean * p.v.mean + p.w.mean * p.w.mean);
    p.direction = qRadiansToDegrees(std::atan2(p.v.mean, p.u.mean));
    return p;
}

QVector<double> instantaneousTke(const QVector<double> &u, const QVector<double> &v,
                                 const QVector<double> &w)
{
    const double meanU = nanMean(u);
    const double meanV = nanMean(v);
    const double meanW = nanMean(w);
    const int n = std::min({u.size(), v.size(), w.size()});
    QVector<double> k(n);
    for (int i = 0; i < n; ++i) {
        const double du = u[i] - meanU;
        const double dv = v[i] - meanV;
        const double dw = w[i] - meanW;
        k[i] = 0.5 * (du * du + dv * dv + dw * dw);
    }
    return k;
}

void welchPsd(const QVector<double> &values, double samplingFrequency,
              QVector<double> *frequencies, QVector<double> *psd)
{
    frequencies->clear();
    psd->clear();

    // fill NaN gaps with the series mean so the FFT stays defined
    const double mean = nanMean(values);
    if (!std::isfinite(mean))
        return;
    QVector<double> x(values.size());
    for (int i = 0; i < values.size(); ++i)
        x[i] = std::isfinite(values[i]) ? values[i] - mean : 0.0;

    // segment length: power of two, at most n/2, at least 256, at most 4096
    int nfft = 256;
    while (nfft * 2 <= x.size() / 2 && nfft < 4096)
        nfft *= 2;
    if (x.size() < nfft)
        return;

    const int hop = nfft / 2; // 50% overlap
    const int nSegments = 1 + (x.size() - nfft) / hop;

    // Hann window and its power for normalization
    QVector<double> window(nfft);
    double windowPower = 0.0;
    for (int i = 0; i < nfft; ++i) {
        window[i] = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (nfft - 1)));
        windowPower += window[i] * window[i];
    }

    kiss_fftr_cfg cfg = kiss_fftr_alloc(nfft, 0, nullptr, nullptr);
    QVector<kiss_fft_cpx> spectrum(nfft / 2 + 1);
    QVector<double> segment(nfft);
    QVector<double> accum(nfft / 2 + 1, 0.0);

    for (int s = 0; s < nSegments; ++s) {
        const int offset = s * hop;
        for (int i = 0; i < nfft; ++i)
            segment[i] = x[offset + i] * window[i];
        kiss_fftr(cfg, segment.constData(), spectrum.data());
        for (int i = 0; i < accum.size(); ++i) {
            const double re = spectrum[i].r;
            const double im = spectrum[i].i;
            accum[i] += re * re + im * im;
        }
    }
    kiss_fftr_free(cfg);

    const double scale = 1.0 / (samplingFrequency * windowPower * nSegments);
    frequencies->resize(accum.size());
    psd->resize(accum.size());
    for (int i = 0; i < accum.size(); ++i) {
        (*frequencies)[i] = i * samplingFrequency / nfft;
        // one-sided PSD: double all bins except DC and Nyquist
        double factor = (i == 0 || i == accum.size() - 1) ? 1.0 : 2.0;
        (*psd)[i] = accum[i] * scale * factor;
    }
}

double dissipationRate(const QVector<double> &u, double samplingFrequency)
{
    const double meanU = std::fabs(nanMean(u));
    if (!std::isfinite(meanU) || meanU < 1e-4)
        return nan();

    QVector<double> f, psd;
    welchPsd(u, samplingFrequency, &f, &psd);
    if (f.size() < 8)
        return nan();

    // Inertial subrange fit (Taylor frozen turbulence):
    //   S(f) = C_1 eps^{2/3} (U / 2 pi)^{2/3} f^{-5/3}
    // with the one-dimensional Kolmogorov constant C_1 ~ 0.49.
    // Use the band [max(1 Hz, 4 f0) .. 0.5 f_Nyquist] and take the median of
    // per-bin eps estimates for robustness against noise floors and peaks.
    const double c1 = 0.49;
    const double fNyquist = samplingFrequency / 2.0;
    const double fLow = std::max(1.0, 4.0 * f.at(1));
    const double fHigh = 0.5 * fNyquist;
    if (fHigh <= fLow)
        return nan();

    QVector<double> epsEstimates;
    const double advection = std::pow(meanU / (2.0 * M_PI), 2.0 / 3.0);
    for (int i = 1; i < f.size(); ++i) {
        if (f[i] < fLow || f[i] > fHigh || psd[i] <= 0.0)
            continue;
        const double eps23 = psd[i] * std::pow(f[i], 5.0 / 3.0) / (c1 * advection);
        epsEstimates.append(std::pow(eps23, 1.5));
    }
    if (epsEstimates.size() < 4)
        return nan();
    std::nth_element(epsEstimates.begin(),
                     epsEstimates.begin() + epsEstimates.size() / 2,
                     epsEstimates.end());
    return epsEstimates.at(epsEstimates.size() / 2);
}

} // namespace flowstats
} // namespace adv
