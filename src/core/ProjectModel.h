/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "MeasurementPoint.h"
#include "Rotation.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QUuid>

#include <memory>

namespace adv {

/// Central document model: all measurement points, per-profile rotation
/// corrections, processing settings and (opaque) GUI plot settings.
class ProjectModel : public QObject
{
    Q_OBJECT
public:
    explicit ProjectModel(QObject *parent = nullptr);

    // --- points -------------------------------------------------------------
    const QList<MeasurementPoint> &points() const { return m_points; }
    const MeasurementPoint *point(const QUuid &id) const;
    QUuid addPoint(MeasurementPoint point);
    bool updatePoint(const MeasurementPoint &point);
    bool removePoint(const QUuid &id);
    void clear();

    /// Set the water depth of every point sharing the given x-y position.
    void setWaterDepthAt(double x, double y, double waterDepth);

    /// Ids of all points sharing the x-y position, sorted by z (ascending).
    QList<QUuid> profilePoints(const QString &xyKey) const;
    /// All distinct x-y profile keys in insertion order.
    QStringList profileKeys() const;

    // --- rotation corrections (per x-y profile) ------------------------------
    RotationAngles correction(const QString &xyKey) const;
    void setCorrection(const QString &xyKey, const RotationAngles &angles);

    // --- processing -----------------------------------------------------------
    /// Processed (windowed, despiked, rotated) series; cached per point.
    std::shared_ptr<const ProcessedSeries> processed(const QUuid &id) const;

    /// Which vertical beam feeds w statistics (W1 default, W2 for down-looking).
    Role wRole() const { return m_wRole; }
    void setWRole(Role role);

    int cpuCount() const { return m_cpuCount; }
    void setCpuCount(int count);
    static int maxCpuCount();

    // --- opaque GUI state persisted with the project -------------------------
    QJsonObject plotSettings() const { return m_plotSettings; }
    void setPlotSettings(const QJsonObject &settings) { m_plotSettings = settings; }

signals:
    void pointAdded(const QUuid &id);
    void pointChanged(const QUuid &id);
    void pointRemoved(const QUuid &id);
    void correctionChanged(const QString &xyKey);
    void modelReset();

private:
    void invalidateCache(const QUuid &id);
    void invalidateProfileCache(const QString &xyKey);

    QList<MeasurementPoint> m_points;
    QHash<QString, RotationAngles> m_corrections;
    mutable QHash<QUuid, std::shared_ptr<const ProcessedSeries>> m_cache;
    Role m_wRole = Role::W1;
    int m_cpuCount = 1;
    QJsonObject m_plotSettings;
};

} // namespace adv
