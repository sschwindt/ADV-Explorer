/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "PlotFrame.h"

#include "core/ProjectModel.h"

#include <qcustomplot.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace adv;

namespace {

const QString kTkeColumn = QStringLiteral("TKE inst. (m^2/s^2)");

QCPScatterStyle::ScatterShape markerShape(int shape)
{
    switch (shape) {
    case 1: return QCPScatterStyle::ssSquare;
    case 2: return QCPScatterStyle::ssCircle;
    case 3: return QCPScatterStyle::ssTriangle;
    default: return QCPScatterStyle::ssNone;
    }
}

} // namespace

PlotFrame::PlotFrame(ProjectModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_plot(new QCustomPlot(this))
{
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);

    toolbar->addWidget(new QLabel(tr("Point:")));
    m_pointCombo = new QComboBox(this);
    m_pointCombo->setMinimumWidth(180);
    toolbar->addWidget(m_pointCombo);

    toolbar->addWidget(new QLabel(tr("Data series:")));
    m_columnCombo = new QComboBox(this);
    m_columnCombo->setMinimumWidth(140);
    toolbar->addWidget(m_columnCombo);

    auto *addButton = new QPushButton(tr("Add"), this);
    toolbar->addWidget(addButton);

    toolbar->addSpacing(12);
    toolbar->addWidget(new QLabel(tr("Shown:")));
    m_seriesCombo = new QComboBox(this);
    m_seriesCombo->setMinimumWidth(200);
    toolbar->addWidget(m_seriesCombo);
    auto *styleButton = new QPushButton(tr("Style..."), this);
    toolbar->addWidget(styleButton);
    auto *removeButton = new QPushButton(tr("Remove"), this);
    toolbar->addWidget(removeButton);

    toolbar->addSpacing(12);
    toolbar->addWidget(new QLabel(tr("Palette:")));
    m_paletteCombo = new QComboBox(this);
    m_paletteCombo->addItems({tr("Okabe-Ito"), tr("Tol bright"), tr("Tol muted"), tr("Grayscale")});
    toolbar->addWidget(m_paletteCombo);
    toolbar->addStretch();

    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectLegend);
    m_plot->legend->setVisible(true);
    m_plot->xAxis->setLabel(tr("time (s)"));
    m_plot->yAxis->setLabel(tr("value"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(toolbar);
    layout->addWidget(m_plot, 1);

    connect(addButton, &QPushButton::clicked, this, &PlotFrame::addSeries);
    connect(removeButton, &QPushButton::clicked, this, &PlotFrame::removeSelectedSeries);
    connect(styleButton, &QPushButton::clicked, this, &PlotFrame::editSelectedStyle);
    connect(m_pointCombo, &QComboBox::currentIndexChanged,
            this, &PlotFrame::pointSelectionChanged);
    connect(m_paletteCombo, &QComboBox::currentIndexChanged, this, &PlotFrame::applyPalette);

    connect(m_model, &ProjectModel::pointAdded, this, &PlotFrame::refresh);
    connect(m_model, &ProjectModel::pointChanged, this, &PlotFrame::refresh);
    connect(m_model, &ProjectModel::pointRemoved, this, &PlotFrame::refresh);
    connect(m_model, &ProjectModel::correctionChanged, this, &PlotFrame::refresh);
    connect(m_model, &ProjectModel::modelReset, this, &PlotFrame::refresh);

    refresh();
}

QList<QColor> PlotFrame::paletteColors() const
{
    switch (m_paletteCombo->currentIndex()) {
    case 1: // Paul Tol bright
        return {QColor("#4477AA"), QColor("#EE6677"), QColor("#228833"), QColor("#CCBB44"),
                QColor("#66CCEE"), QColor("#AA3377"), QColor("#BBBBBB")};
    case 2: // Paul Tol muted
        return {QColor("#332288"), QColor("#88CCEE"), QColor("#44AA99"), QColor("#117733"),
                QColor("#999933"), QColor("#DDCC77"), QColor("#CC6677"), QColor("#882255"),
                QColor("#AA4499")};
    case 3: // grayscale
        return {QColor("#000000"), QColor("#555555"), QColor("#888888"), QColor("#BBBBBB")};
    default: // Okabe-Ito
        return {QColor("#0072B2"), QColor("#E69F00"), QColor("#009E73"), QColor("#D55E00"),
                QColor("#CC79A7"), QColor("#56B4E9"), QColor("#F0E442"), QColor("#000000")};
    }
}

void PlotFrame::refresh()
{
    // drop series whose point vanished
    for (int i = m_series.size() - 1; i >= 0; --i) {
        if (!m_model->point(m_series.at(i).pointId))
            m_series.removeAt(i);
    }
    rebuildPointCombo();
    rebuildSeriesCombo();
    rebuildPlot();
}

void PlotFrame::rebuildPointCombo()
{
    const QVariant current = m_pointCombo->currentData();
    m_pointCombo->blockSignals(true);
    m_pointCombo->clear();
    for (const MeasurementPoint &point : m_model->points())
        m_pointCombo->addItem(point.label(), point.id.toString());
    const int index = m_pointCombo->findData(current);
    m_pointCombo->setCurrentIndex(index >= 0 ? index : 0);
    m_pointCombo->blockSignals(false);
    rebuildColumnCombo();
}

void PlotFrame::rebuildColumnCombo()
{
    const QString current = m_columnCombo->currentText();
    m_columnCombo->clear();
    const QUuid pointId = QUuid::fromString(m_pointCombo->currentData().toString());
    const MeasurementPoint *point = m_model->point(pointId);
    if (!point)
        return;
    for (const QString &name : point->data.columnNames()) {
        if (name != roleName(Role::Time))
            m_columnCombo->addItem(name);
    }
    m_columnCombo->addItem(kTkeColumn);
    const int index = m_columnCombo->findText(current);
    if (index >= 0)
        m_columnCombo->setCurrentIndex(index);
}

void PlotFrame::rebuildSeriesCombo()
{
    const int current = m_seriesCombo->currentIndex();
    m_seriesCombo->clear();
    for (const Series &series : m_series)
        m_seriesCombo->addItem(seriesLabel(series));
    if (current >= 0 && current < m_seriesCombo->count())
        m_seriesCombo->setCurrentIndex(current);
}

void PlotFrame::pointSelectionChanged()
{
    rebuildColumnCombo();
}

QString PlotFrame::seriesLabel(const Series &series) const
{
    const MeasurementPoint *point = m_model->point(series.pointId);
    return point ? QStringLiteral("%1 | %2").arg(point->label(), series.column)
                 : series.column;
}

void PlotFrame::addSeries()
{
    const QUuid pointId = QUuid::fromString(m_pointCombo->currentData().toString());
    if (!m_model->point(pointId) || m_columnCombo->currentText().isEmpty())
        return;
    Series series;
    series.pointId = pointId;
    series.column = m_columnCombo->currentText();
    const QList<QColor> colors = paletteColors();
    series.style.lineColor = colors.at(m_series.size() % colors.size());
    series.style.markerColor = series.style.lineColor;
    m_series.append(series);
    rebuildSeriesCombo();
    m_seriesCombo->setCurrentIndex(m_series.size() - 1);
    rebuildPlot();
}

void PlotFrame::removeSelectedSeries()
{
    const int index = m_seriesCombo->currentIndex();
    if (index < 0 || index >= m_series.size())
        return;
    m_series.removeAt(index);
    rebuildSeriesCombo();
    rebuildPlot();
}

void PlotFrame::editSelectedStyle()
{
    const int index = m_seriesCombo->currentIndex();
    if (index < 0 || index >= m_series.size())
        return;
    SeriesStyleDialog dialog(m_series.at(index).style, this);
    if (dialog.exec() == QDialog::Accepted) {
        m_series[index].style = dialog.style();
        rebuildPlot();
    }
}

void PlotFrame::applyPalette()
{
    const QList<QColor> colors = paletteColors();
    for (int i = 0; i < m_series.size(); ++i) {
        m_series[i].style.lineColor = colors.at(i % colors.size());
        m_series[i].style.markerColor = m_series[i].style.lineColor;
    }
    rebuildPlot();
}

bool PlotFrame::seriesData(const Series &series, QVector<double> *t, QVector<double> *y) const
{
    const MeasurementPoint *point = m_model->point(series.pointId);
    if (!point)
        return false;
    const auto processed = m_model->processed(series.pointId);
    if (!processed || !processed->isValid())
        return false;

    *t = processed->time;
    if (series.column == kTkeColumn) {
        *y = processed->tkeInst;
        return true;
    }
    const Role role = roleFromName(series.column);
    switch (role) {
    case Role::U: *y = processed->u; return true;
    case Role::V: *y = processed->v; return true;
    case Role::W1: *y = processed->w1; return true;
    case Role::W2: *y = processed->w2; return !y->isEmpty();
    default: break;
    }
    // non-velocity columns: raw values, sliced to the analysis window
    const int columnIndex = point->data.columnNames().indexOf(series.column);
    if (columnIndex < 0)
        return false;
    const QVector<double> &full = point->data.column(columnIndex);
    const QVector<double> &fullTime = point->data.columnByRole(Role::Time);
    int first = 0;
    while (first < fullTime.size() && !t->isEmpty() && fullTime.at(first) < t->first())
        ++first;
    *y = full.mid(first, t->size());
    return !y->isEmpty();
}

void PlotFrame::rebuildPlot()
{
    m_plot->clearGraphs();
    for (const Series &series : m_series) {
        QVector<double> t, y;
        if (!seriesData(series, &t, &y))
            continue;
        QCPGraph *graph = m_plot->addGraph();
        graph->setData(t, y, true);
        graph->setName(seriesLabel(series));

        const SeriesStyle &style = series.style;
        QPen pen(style.lineColor);
        pen.setWidthF(style.lineWidth);
        pen.setStyle(style.lineWidth > 0.0 ? style.lineStyle : Qt::NoPen);
        graph->setPen(pen);
        graph->setLineStyle(style.lineWidth > 0.0 ? QCPGraph::lsLine : QCPGraph::lsNone);

        if (style.markerShape > 0) {
            QPen markerPen(style.markerColor);
            markerPen.setWidthF(style.markerLineWidth);
            graph->setScatterStyle(QCPScatterStyle(
                markerShape(style.markerShape),
                markerPen,
                style.markerFilled ? QBrush(style.markerColor) : Qt::NoBrush,
                style.markerSize));
        }
    }
    m_plot->rescaleAxes();
    m_plot->replot();
}

bool PlotFrame::exportPng(const QString &filePath, int dpi)
{
    // export exactly the current widget proportions at the requested dpi
    const double scale = dpi / 96.0;
    return m_plot->savePng(filePath, m_plot->width(), m_plot->height(), scale, -1, dpi);
}

QList<PlotFrame::ExportSeries> PlotFrame::currentSeries() const
{
    QList<ExportSeries> result;
    for (const Series &series : m_series) {
        ExportSeries out;
        if (!seriesData(series, &out.time, &out.values))
            continue;
        out.label = seriesLabel(series);
        result.append(out);
    }
    return result;
}

QJsonObject PlotFrame::saveState() const
{
    QJsonObject state;
    state[QStringLiteral("palette")] = m_paletteCombo->currentIndex();
    QJsonArray seriesArray;
    for (const Series &series : m_series) {
        QJsonObject o;
        o[QStringLiteral("pointId")] = series.pointId.toString(QUuid::WithoutBraces);
        o[QStringLiteral("column")] = series.column;
        o[QStringLiteral("style")] = series.style.toJson();
        seriesArray.append(o);
    }
    state[QStringLiteral("series")] = seriesArray;
    return state;
}

void PlotFrame::restoreState(const QJsonObject &state)
{
    m_paletteCombo->setCurrentIndex(state[QStringLiteral("palette")].toInt(0));
    m_series.clear();
    for (const QJsonValue &value : state[QStringLiteral("series")].toArray()) {
        const QJsonObject o = value.toObject();
        Series series;
        series.pointId = QUuid::fromString(o[QStringLiteral("pointId")].toString());
        series.column = o[QStringLiteral("column")].toString();
        series.style = SeriesStyle::fromJson(o[QStringLiteral("style")].toObject());
        if (m_model->point(series.pointId))
            m_series.append(series);
    }
    rebuildSeriesCombo();
    rebuildPlot();
}
