/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "ProjectModel.h"

#include <QThread>

#include <algorithm>

namespace adv {

ProjectModel::ProjectModel(QObject *parent)
    : QObject(parent)
    , m_cpuCount(std::max(1, QThread::idealThreadCount() / 2))
{
}

const MeasurementPoint *ProjectModel::point(const QUuid &id) const
{
    for (const MeasurementPoint &p : m_points) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

QUuid ProjectModel::addPoint(MeasurementPoint point)
{
    if (point.id.isNull())
        point.id = QUuid::createUuid();
    const QUuid id = point.id;
    const bool depthSet = point.hasWaterDepth();
    const double x = point.x, y = point.y, depth = point.waterDepth;
    m_points.append(std::move(point));
    if (depthSet)
        setWaterDepthAt(x, y, depth);
    emit pointAdded(id);
    return id;
}

bool ProjectModel::updatePoint(const MeasurementPoint &point)
{
    for (MeasurementPoint &p : m_points) {
        if (p.id == point.id) {
            const QString oldKey = p.xyKey();
            p = point;
            invalidateCache(point.id);
            if (point.hasWaterDepth())
                setWaterDepthAt(point.x, point.y, point.waterDepth);
            if (oldKey != point.xyKey())
                invalidateProfileCache(oldKey);
            emit pointChanged(point.id);
            return true;
        }
    }
    return false;
}

bool ProjectModel::removePoint(const QUuid &id)
{
    for (int i = 0; i < m_points.size(); ++i) {
        if (m_points.at(i).id == id) {
            m_points.removeAt(i);
            m_cache.remove(id);
            emit pointRemoved(id);
            return true;
        }
    }
    return false;
}

void ProjectModel::clear()
{
    m_points.clear();
    m_corrections.clear();
    m_cache.clear();
    m_plotSettings = QJsonObject();
    emit modelReset();
}

void ProjectModel::setWaterDepthAt(double x, double y, double waterDepth)
{
    const QString key = MeasurementPoint::makeXyKey(x, y);
    for (MeasurementPoint &p : m_points) {
        if (p.xyKey() == key && p.waterDepth != waterDepth) {
            p.waterDepth = waterDepth;
            emit pointChanged(p.id);
        }
    }
}

QList<QUuid> ProjectModel::profilePoints(const QString &xyKey) const
{
    QList<QPair<double, QUuid>> zAndId;
    for (const MeasurementPoint &p : m_points) {
        if (p.xyKey() == xyKey)
            zAndId.append({p.z, p.id});
    }
    std::sort(zAndId.begin(), zAndId.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<QUuid> ids;
    for (const auto &pair : zAndId)
        ids.append(pair.second);
    return ids;
}

QStringList ProjectModel::profileKeys() const
{
    QStringList keys;
    for (const MeasurementPoint &p : m_points) {
        if (!keys.contains(p.xyKey()))
            keys.append(p.xyKey());
    }
    return keys;
}

RotationAngles ProjectModel::correction(const QString &xyKey) const
{
    return m_corrections.value(xyKey);
}

void ProjectModel::setCorrection(const QString &xyKey, const RotationAngles &angles)
{
    m_corrections.insert(xyKey, angles);
    invalidateProfileCache(xyKey);
    emit correctionChanged(xyKey);
}

std::shared_ptr<const ProcessedSeries> ProjectModel::processed(const QUuid &id) const
{
    if (const auto cached = m_cache.value(id))
        return cached;
    const MeasurementPoint *p = point(id);
    if (!p)
        return nullptr;
    auto series = std::make_shared<ProcessedSeries>(
        processPoint(*p, correction(p->xyKey()), m_wRole));
    m_cache.insert(id, series);
    return series;
}

void ProjectModel::setWRole(Role role)
{
    if (role != Role::W1 && role != Role::W2)
        return;
    if (m_wRole == role)
        return;
    m_wRole = role;
    m_cache.clear();
    emit modelReset();
}

void ProjectModel::setCpuCount(int count)
{
    m_cpuCount = std::clamp(count, 1, maxCpuCount());
}

int ProjectModel::maxCpuCount()
{
    return std::max(1, QThread::idealThreadCount());
}

void ProjectModel::invalidateCache(const QUuid &id)
{
    m_cache.remove(id);
}

void ProjectModel::invalidateProfileCache(const QString &xyKey)
{
    for (const MeasurementPoint &p : m_points) {
        if (p.xyKey() == xyKey)
            m_cache.remove(p.id);
    }
}

} // namespace adv
