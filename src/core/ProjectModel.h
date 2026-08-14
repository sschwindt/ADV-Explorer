/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "MeasurementPoint.h"
#include "ProjectSettings.h"
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

    // --- mode and georeferencing ---------------------------------------------
    /// Lab (flume) or field (georeferenced river) campaign; Lab by default.
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    /// EPSG code the x-y coordinates are expressed in when in field mode.
    /// 0 means "not chosen yet"; setting an unsupported code fails and keeps
    /// the previous value.
    int epsg() const { return m_epsg; }
    bool setEpsg(int epsg);

    /// Surveyed cross sections, used to place stations by chainage and to draw
    /// the section on the map.
    const QList<CrossSection> &crossSections() const { return m_crossSections; }
    void setCrossSections(const QList<CrossSection> &sections);
    void addCrossSection(const CrossSection &section);

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
    void modeChanged(adv::Mode mode);
    void crsChanged(int epsg);
    void crossSectionsChanged();

private:
    void invalidateCache(const QUuid &id);
    void invalidateProfileCache(const QString &xyKey);

    QList<MeasurementPoint> m_points;
    QHash<QString, RotationAngles> m_corrections;
    mutable QHash<QUuid, std::shared_ptr<const ProcessedSeries>> m_cache;
    Role m_wRole = Role::W1;
    int m_cpuCount = 1;
    QJsonObject m_plotSettings;
    Mode m_mode = Mode::Lab;
    int m_epsg = 0;
    QList<CrossSection> m_crossSections;
};

} // namespace adv
