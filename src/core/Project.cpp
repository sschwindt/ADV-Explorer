/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "Project.h"

#include "Crs.h"
#include "CsvReader.h"
#include "FormatRegistry.h"
#include "ProjectModel.h"
#include "VnaReader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>

namespace adv {
namespace project {

namespace {

/// 1: original layout.
/// 2: adds the campaign mode, the project coordinate system and cross sections.
///    Points are unchanged, so version 1 files load without any migration.
constexpr int kFormatVersion = 2;

// NaN-safe number <-> JSON conversion (JSON has no NaN)
QJsonValue toJson(double value)
{
    return std::isfinite(value) ? QJsonValue(value) : QJsonValue();
}

double fromJson(const QJsonValue &value)
{
    return value.isDouble() ? value.toDouble() : nan();
}

QJsonObject despikeToJson(const DespikeConfig &c)
{
    QJsonObject o;
    o[QStringLiteral("corrEnabled")] = c.corrEnabled;
    o[QStringLiteral("corrThreshold")] = c.corrThreshold;
    o[QStringLiteral("snrEnabled")] = c.snrEnabled;
    o[QStringLiteral("snrThreshold")] = c.snrThreshold;
    o[QStringLiteral("velEnabled")] = c.velEnabled;
    o[QStringLiteral("velK")] = c.velK;
    o[QStringLiteral("gnMethod")] = int(c.gnMethod);
    o[QStringLiteral("gnLambdaA")] = c.gnLambdaA;
    o[QStringLiteral("gnK")] = c.gnK;
    o[QStringLiteral("pstEnabled")] = c.pstEnabled;
    o[QStringLiteral("replace")] = int(c.replace);
    return o;
}

DespikeConfig despikeFromJson(const QJsonObject &o)
{
    DespikeConfig c;
    c.corrEnabled = o[QStringLiteral("corrEnabled")].toBool();
    c.corrThreshold = o[QStringLiteral("corrThreshold")].toDouble(70.0);
    c.snrEnabled = o[QStringLiteral("snrEnabled")].toBool();
    c.snrThreshold = o[QStringLiteral("snrThreshold")].toDouble(20.0);
    c.velEnabled = o[QStringLiteral("velEnabled")].toBool();
    c.velK = o[QStringLiteral("velK")].toDouble(3.0);
    c.gnMethod = DespikeConfig::GnMethod(o[QStringLiteral("gnMethod")].toInt(0));
    c.gnLambdaA = o[QStringLiteral("gnLambdaA")].toDouble(1.0);
    c.gnK = o[QStringLiteral("gnK")].toDouble(3.0);
    c.pstEnabled = o[QStringLiteral("pstEnabled")].toBool();
    c.replace = DespikeConfig::Replace(
        o[QStringLiteral("replace")].toInt(int(DespikeConfig::Replace::LinearInterpolation)));
    return c;
}

QJsonObject pointToJson(const MeasurementPoint &p)
{
    QJsonObject o;
    o[QStringLiteral("id")] = p.id.toString(QUuid::WithoutBraces);
    o[QStringLiteral("x")] = p.x;
    o[QStringLiteral("y")] = p.y;
    o[QStringLiteral("z")] = p.z;
    o[QStringLiteral("waterDepth")] = toJson(p.waterDepth);
    o[QStringLiteral("tStart")] = toJson(p.tStart);
    o[QStringLiteral("tEnd")] = toJson(p.tEnd);
    o[QStringLiteral("despike")] = despikeToJson(p.despike);
    if (!p.stationName.isEmpty())
        o[QStringLiteral("stationName")] = p.stationName;
    o[QStringLiteral("chainage")] = toJson(p.chainage);

    QJsonObject file;
    file[QStringLiteral("name")] = p.data.sourceFileName();
    file[QStringLiteral("format")] = p.data.format();
    file[QStringLiteral("samplingFrequency")] = p.data.samplingFrequency();
    file[QStringLiteral("timeSynthesized")] = p.data.timeSynthesized();
    file[QStringLiteral("dataB64")] =
        QString::fromLatin1(qCompress(p.data.rawBytes(), 9).toBase64());
    // persist the CSV role mapping so restoring reproduces the columns
    QJsonObject mapping;
    const QHash<Role, int> roles = p.data.roleMap();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        mapping[roleName(it.key())] = it.value();
    file[QStringLiteral("mapping")] = mapping;
    o[QStringLiteral("file")] = file;
    return o;
}

bool pointFromJson(const QJsonObject &o, MeasurementPoint *p, QString *errorString)
{
    p->id = QUuid::fromString(o[QStringLiteral("id")].toString());
    p->x = o[QStringLiteral("x")].toDouble();
    p->y = o[QStringLiteral("y")].toDouble();
    p->z = o[QStringLiteral("z")].toDouble();
    p->waterDepth = fromJson(o[QStringLiteral("waterDepth")]);
    p->tStart = fromJson(o[QStringLiteral("tStart")]);
    p->tEnd = fromJson(o[QStringLiteral("tEnd")]);
    p->despike = despikeFromJson(o[QStringLiteral("despike")].toObject());
    p->stationName = o[QStringLiteral("stationName")].toString();
    p->chainage = fromJson(o[QStringLiteral("chainage")]);

    const QJsonObject file = o[QStringLiteral("file")].toObject();
    const QByteArray raw = qUncompress(
        QByteArray::fromBase64(file[QStringLiteral("dataB64")].toString().toLatin1()));
    if (raw.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("Embedded data of point %1 cannot be decompressed.")
                               .arg(p->label());
        return false;
    }
    const QString format = file[QStringLiteral("format")].toString(QStringLiteral("vna"));
    const FileFormat *reader = formats::byId(format);
    if (!reader) {
        if (errorString)
            *errorString = QStringLiteral("Point %1 was stored in the unknown data format \"%2\".")
                               .arg(p->label(), format);
        return false;
    }

    QHash<Role, int> mapping;
    const QJsonObject mappingObj = file[QStringLiteral("mapping")].toObject();
    for (auto it = mappingObj.constBegin(); it != mappingObj.constEnd(); ++it)
        mapping.insert(roleFromName(it.key()), it.value().toInt());

    QString readError;
    p->data = reader->read(raw, mapping, &readError);
    if (p->data.isEmpty()) {
        if (errorString)
            *errorString = QStringLiteral("Embedded data of point %1 cannot be parsed: %2")
                               .arg(p->label(), readError);
        return false;
    }
    p->data.setSourceFileName(file[QStringLiteral("name")].toString());
    // restore the user-provided time base of files without a time column
    if (file[QStringLiteral("timeSynthesized")].toBool())
        p->data.synthesizeTime(file[QStringLiteral("samplingFrequency")].toDouble(200.0));
    return true;
}

QJsonObject crossSectionToJson(const CrossSection &section)
{
    QJsonObject o;
    o[QStringLiteral("name")] = section.name;
    o[QStringLiteral("leftX")] = toJson(section.leftX);
    o[QStringLiteral("leftY")] = toJson(section.leftY);
    o[QStringLiteral("rightX")] = toJson(section.rightX);
    o[QStringLiteral("rightY")] = toJson(section.rightY);
    o[QStringLiteral("leftChainage")] = toJson(section.leftChainage);
    o[QStringLiteral("rightChainage")] = toJson(section.rightChainage);

    QJsonArray bed;
    for (const QPointF &node : section.bed) {
        QJsonObject b;
        b[QStringLiteral("chainage")] = node.x();
        b[QStringLiteral("depth")] = node.y();
        bed.append(b);
    }
    o[QStringLiteral("bed")] = bed;
    return o;
}

CrossSection crossSectionFromJson(const QJsonObject &o)
{
    CrossSection section;
    section.name = o[QStringLiteral("name")].toString();
    section.leftX = fromJson(o[QStringLiteral("leftX")]);
    section.leftY = fromJson(o[QStringLiteral("leftY")]);
    section.rightX = fromJson(o[QStringLiteral("rightX")]);
    section.rightY = fromJson(o[QStringLiteral("rightY")]);
    section.leftChainage = fromJson(o[QStringLiteral("leftChainage")]);
    section.rightChainage = fromJson(o[QStringLiteral("rightChainage")]);
    for (const QJsonValue &value : o[QStringLiteral("bed")].toArray()) {
        const QJsonObject b = value.toObject();
        section.bed.append(QPointF(b[QStringLiteral("chainage")].toDouble(),
                                   b[QStringLiteral("depth")].toDouble()));
    }
    return section;
}

} // namespace

bool save(const ProjectModel &model, const QString &filePath, QString *errorString)
{
    QJsonObject root;
    root[QStringLiteral("application")] = QStringLiteral("ADV-Explorer");
    root[QStringLiteral("formatVersion")] = kFormatVersion;
    root[QStringLiteral("wComponent")] =
        model.wRole() == Role::W2 ? QStringLiteral("w2") : QStringLiteral("w1");
    root[QStringLiteral("cpuCount")] = model.cpuCount();
    root[QStringLiteral("plotSettings")] = model.plotSettings();
    root[QStringLiteral("mode")] = modeId(model.mode());
    root[QStringLiteral("epsg")] = model.epsg();

    QJsonArray sections;
    for (const CrossSection &section : model.crossSections())
        sections.append(crossSectionToJson(section));
    root[QStringLiteral("crossSections")] = sections;

    QJsonArray corrections;
    for (const QString &key : model.profileKeys()) {
        const RotationAngles angles = model.correction(key);
        if (angles.isIdentity())
            continue;
        QJsonObject c;
        c[QStringLiteral("xyKey")] = key;
        c[QStringLiteral("heading")] = angles.heading;
        c[QStringLiteral("pitch")] = angles.pitch;
        c[QStringLiteral("roll")] = angles.roll;
        corrections.append(c);
    }
    root[QStringLiteral("corrections")] = corrections;

    QJsonArray points;
    for (const MeasurementPoint &p : model.points())
        points.append(pointToJson(p));
    root[QStringLiteral("points")] = points;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot write %1: %2").arg(filePath, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool load(ProjectModel *model, const QString &filePath, QString *errorString, QString *warning)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (errorString)
            *errorString = QStringLiteral("Not a valid ADV-Explorer project file: %1")
                               .arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = doc.object();
    if (root[QStringLiteral("application")].toString() != QStringLiteral("ADV-Explorer")) {
        if (errorString)
            *errorString = QStringLiteral("Not an ADV-Explorer project file.");
        return false;
    }
    // the version was written but never checked before format 2, so a file from
    // a newer build used to load silently and partially
    const int version = root[QStringLiteral("formatVersion")].toInt(1);
    if (version > kFormatVersion) {
        if (errorString)
            *errorString = QStringLiteral(
                               "%1 was written by a newer version of ADV-Explorer "
                               "(project format %2, this build reads up to %3).")
                               .arg(QFileInfo(filePath).fileName())
                               .arg(version).arg(kFormatVersion);
        return false;
    }

    model->clear();
    // absent in version 1 files, which are always laboratory projects
    model->setMode(modeFromId(root[QStringLiteral("mode")].toString()));
    const int epsg = root[QStringLiteral("epsg")].toInt(0);
    if (!model->setEpsg(epsg)) {
        // an unsupported system must not make the whole project unopenable
        model->setEpsg(0);
        model->setMode(Mode::Lab);
        if (warning)
            *warning = QStringLiteral(
                           "EPSG:%1 is not supported, so the project was opened in "
                           "laboratory mode. %2")
                           .arg(epsg).arg(crs::supportedRangesText());
    }

    QList<CrossSection> sections;
    for (const QJsonValue &value : root[QStringLiteral("crossSections")].toArray())
        sections.append(crossSectionFromJson(value.toObject()));
    model->setCrossSections(sections);
    model->setWRole(root[QStringLiteral("wComponent")].toString() == QStringLiteral("w2")
                        ? Role::W2 : Role::W1);
    model->setCpuCount(root[QStringLiteral("cpuCount")].toInt(model->cpuCount()));
    model->setPlotSettings(root[QStringLiteral("plotSettings")].toObject());

    for (const QJsonValue &value : root[QStringLiteral("points")].toArray()) {
        MeasurementPoint p;
        if (!pointFromJson(value.toObject(), &p, errorString))
            return false;
        model->addPoint(std::move(p));
    }

    for (const QJsonValue &value : root[QStringLiteral("corrections")].toArray()) {
        const QJsonObject c = value.toObject();
        RotationAngles angles;
        angles.heading = c[QStringLiteral("heading")].toDouble();
        angles.pitch = c[QStringLiteral("pitch")].toDouble();
        angles.roll = c[QStringLiteral("roll")].toDouble();
        model->setCorrection(c[QStringLiteral("xyKey")].toString(), angles);
    }
    return true;
}

QString fileExtension()
{
    return QStringLiteral("advProj");
}

QString fileFilter()
{
    return QStringLiteral("ADV-Explorer project (*.advProj)");
}

} // namespace project
} // namespace adv
