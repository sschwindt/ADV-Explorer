/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "ProfileStatsExport.h"

#include "ProjectModel.h"

#include <xlsxdocument.h>

#include <QFile>

#include <cmath>

namespace adv {
namespace statsexport {

namespace {

/// Column letter (A, B, ..., AA, ...) of a 1-based column index.
QString columnLetter(int column)
{
    QString letters;
    while (column > 0) {
        const int rem = (column - 1) % 26;
        letters.prepend(QChar('A' + rem));
        column = (column - 1) / 26;
    }
    return letters;
}

void writeNumber(QXlsx::Document &doc, int row, int col, double value)
{
    if (std::isfinite(value))
        doc.write(row, col, value);
    else
        doc.write(row, col, QStringLiteral("nan"));
}

} // namespace

QStringList profileTemplateColumns()
{
    return {
        QStringLiteral("profile"),
        QStringLiteral("x (m)"), QStringLiteral("y (m)"), QStringLiteral("z (m)"),
        QStringLiteral("h (m)"), QStringLiteral("z/h (-)"),
        QStringLiteral("u mean (m/s)"), QStringLiteral("u std (m/s)"),
        QStringLiteral("u skewness (-)"), QStringLiteral("u kurtosis (-)"),
        QStringLiteral("v mean (m/s)"), QStringLiteral("v std (m/s)"),
        QStringLiteral("v skewness (-)"), QStringLiteral("v kurtosis (-)"),
        QStringLiteral("w mean (m/s)"), QStringLiteral("w std (m/s)"),
        QStringLiteral("w skewness (-)"), QStringLiteral("w kurtosis (-)"),
        QStringLiteral("u'v' (m^2/s^2)"), QStringLiteral("u'w' (m^2/s^2)"),
        QStringLiteral("v'w' (m^2/s^2)"),
        QStringLiteral("TKE (m^2/s^2)"), QStringLiteral("eps (m^2/s^3)"),
        QStringLiteral("U magnitude (m/s)"), QStringLiteral("direction (deg)"),
    };
}

bool writePointStats(const ProjectModel &model, const QString &filePath,
                     QString *errorString)
{
    QXlsx::Document doc;
    const QStringList headers = {
        QStringLiteral("file"),
        QStringLiteral("x (m)"), QStringLiteral("y (m)"), QStringLiteral("z (m)"),
        QStringLiteral("h (m)"), QStringLiteral("z/h (-)"),
        QStringLiteral("t start (s)"), QStringLiteral("t end (s)"),
        QStringLiteral("u mean (m/s)"), QStringLiteral("u std (m/s)"),
        QStringLiteral("u stderr (m/s)"), QStringLiteral("u skewness (-)"),
        QStringLiteral("u kurtosis (-)"),
        QStringLiteral("v mean (m/s)"), QStringLiteral("v std (m/s)"),
        QStringLiteral("v stderr (m/s)"), QStringLiteral("v skewness (-)"),
        QStringLiteral("v kurtosis (-)"),
        QStringLiteral("w mean (m/s)"), QStringLiteral("w std (m/s)"),
        QStringLiteral("w stderr (m/s)"), QStringLiteral("w skewness (-)"),
        QStringLiteral("w kurtosis (-)"),
        QStringLiteral("u'v' (m^2/s^2)"), QStringLiteral("u'w' (m^2/s^2)"),
        QStringLiteral("v'w' (m^2/s^2)"),
        QStringLiteral("TKE (m^2/s^2)"), QStringLiteral("eps (m^2/s^3)"),
        QStringLiteral("U magnitude (m/s)"), QStringLiteral("direction (deg)"),
        QStringLiteral("removed spikes"),
    };
    for (int c = 0; c < headers.size(); ++c)
        doc.write(1, c + 1, headers.at(c));

    int row = 2;
    for (const MeasurementPoint &p : model.points()) {
        const auto series = model.processed(p.id);
        if (!series || !series->isValid())
            continue;
        const PointStats &s = series->stats;
        int col = 1;
        doc.write(row, col++, p.data.sourceFileName());
        writeNumber(doc, row, col++, p.x);
        writeNumber(doc, row, col++, p.y);
        writeNumber(doc, row, col++, p.z);
        writeNumber(doc, row, col++, p.waterDepth);
        writeNumber(doc, row, col++, p.hasWaterDepth() ? p.z / p.waterDepth : nan());
        writeNumber(doc, row, col++, series->time.isEmpty() ? nan() : series->time.first());
        writeNumber(doc, row, col++, series->time.isEmpty() ? nan() : series->time.last());
        for (const SeriesStats *comp : {&s.u, &s.v, &s.w}) {
            writeNumber(doc, row, col++, comp->mean);
            writeNumber(doc, row, col++, comp->std);
            writeNumber(doc, row, col++, comp->stderror);
            writeNumber(doc, row, col++, comp->skewness);
            writeNumber(doc, row, col++, comp->kurtosis);
        }
        writeNumber(doc, row, col++, s.uv);
        writeNumber(doc, row, col++, s.uw);
        writeNumber(doc, row, col++, s.vw);
        writeNumber(doc, row, col++, s.tke);
        writeNumber(doc, row, col++, s.eps);
        writeNumber(doc, row, col++, s.magnitude);
        writeNumber(doc, row, col++, s.direction);
        int spikes = 0;
        for (int n : series->spikeCounts)
            spikes += n;
        doc.write(row, col++, spikes);
        ++row;
    }

    if (!doc.saveAs(filePath)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot write %1").arg(filePath);
        return false;
    }
    return true;
}

bool fillProfileTemplate(const ProjectModel &model, const QString &templatePath,
                         const QString &outputPath, QString *errorString)
{
    // work on the template when available, otherwise start from scratch
    QXlsx::Document doc(QFile::exists(templatePath) ? templatePath : QString());
    const QStringList columns = profileTemplateColumns();

    // template layout: row 1 title, row 2 column headers, data from row 3
    for (int c = 0; c < columns.size(); ++c)
        doc.write(2, c + 1, columns.at(c));
    if (doc.read(1, 1).toString().isEmpty())
        doc.write(1, 1, QStringLiteral("ADV-Explorer vertical profile statistics"));

    // column indices (1-based) of the mean velocities for the formulas
    const int colUMean = columns.indexOf(QStringLiteral("u mean (m/s)")) + 1;
    const int colVMean = columns.indexOf(QStringLiteral("v mean (m/s)")) + 1;
    const int colWMean = columns.indexOf(QStringLiteral("w mean (m/s)")) + 1;
    const int colMag = columns.indexOf(QStringLiteral("U magnitude (m/s)")) + 1;
    const int colDir = columns.indexOf(QStringLiteral("direction (deg)")) + 1;

    int row = 3;
    for (const QString &key : model.profileKeys()) {
        for (const QUuid &id : model.profilePoints(key)) {
            const MeasurementPoint *p = model.point(id);
            const auto series = model.processed(id);
            if (!p || !series || !series->isValid())
                continue;
            const PointStats &s = series->stats;
            int col = 1;
            doc.write(row, col++, key);
            writeNumber(doc, row, col++, p->x);
            writeNumber(doc, row, col++, p->y);
            writeNumber(doc, row, col++, p->z);
            writeNumber(doc, row, col++, p->waterDepth);
            writeNumber(doc, row, col++, p->hasWaterDepth() ? p->z / p->waterDepth : nan());
            for (const SeriesStats *comp : {&s.u, &s.v, &s.w}) {
                writeNumber(doc, row, col++, comp->mean);
                writeNumber(doc, row, col++, comp->std);
                writeNumber(doc, row, col++, comp->skewness);
                writeNumber(doc, row, col++, comp->kurtosis);
            }
            writeNumber(doc, row, col++, s.uv);
            writeNumber(doc, row, col++, s.uw);
            writeNumber(doc, row, col++, s.vw);
            writeNumber(doc, row, col++, s.tke);
            writeNumber(doc, row, col++, s.eps);
            // formulas evaluating magnitude and direction from the mean columns
            const QString u = columnLetter(colUMean) + QString::number(row);
            const QString v = columnLetter(colVMean) + QString::number(row);
            const QString w = columnLetter(colWMean) + QString::number(row);
            doc.write(row, colMag,
                      QStringLiteral("=SQRT(%1^2+%2^2+%3^2)").arg(u, v, w));
            doc.write(row, colDir,
                      QStringLiteral("=DEGREES(ATAN2(%1,%2))").arg(u, v));
            ++row;
        }
    }

    if (!doc.saveAs(outputPath)) {
        if (errorString)
            *errorString = QStringLiteral("Cannot write %1").arg(outputPath);
        return false;
    }
    return true;
}

} // namespace statsexport
} // namespace adv
