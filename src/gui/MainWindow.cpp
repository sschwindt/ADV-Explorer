/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "MainWindow.h"

#include "AboutDialog.h"
#include "FlumeView.h"
#include "ImportWizard.h"
#include "PlotFrame.h"
#include "PointWizard.h"
#include "ProfileFrame.h"

#include "core/CsvReader.h"
#include "core/ProfileStatsExport.h"
#include "core/Project.h"
#include "core/ProjectModel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

using namespace adv;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_model(new ProjectModel(this))
{
    setWindowTitle(QStringLiteral("ADV-Explorer"));
    resize(1280, 860);

    // subtle background image for a modern app look
    setStyleSheet(QStringLiteral(
        "QMainWindow { background-image: url(:/img/app-background.jpg);"
        " background-position: center; }"));

    m_flumeView = new FlumeView(m_model, this);
    connect(m_flumeView, &FlumeView::newPointRequested, this, &MainWindow::createPointAt);
    connect(m_flumeView, &FlumeView::editPointRequested, this, &MainWindow::editPoint);

    // time-series tab with a stackable column of plot frames
    auto *plotContainer = new QWidget(this);
    m_plotColumn = new QVBoxLayout(plotContainer);
    m_plotColumn->setContentsMargins(0, 0, 0, 0);
    auto *firstFrame = new PlotFrame(m_model, plotContainer);
    m_plotFrames.append(firstFrame);
    m_plotColumn->addWidget(firstFrame);

    m_profileFrame = new ProfileFrame(m_model, this);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(plotContainer, tr("Time series"));
    m_tabs->addTab(m_profileFrame, tr("Vertical profiles"));

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_flumeView);
    splitter->addWidget(m_tabs);
    // flume view gets 2/5 of the height (20% more than the earlier 1/3)
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({2 * height() / 5, 3 * height() / 5});
    setCentralWidget(splitter);

    buildMenus();
    statusBar()->showMessage(tr("Click into the flume to define a measurement point."));
}

void MainWindow::buildMenus()
{
    // --- File ---------------------------------------------------------------
    // note: the addAction(text, shortcut, receiver, slot) convenience overload
    // only exists since Qt 6.3; set shortcuts explicitly for Qt 6.2 support
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New project"), this, &MainWindow::newProject)
        ->setShortcut(QKeySequence::New);
    fileMenu->addAction(tr("&Open project..."), this, &MainWindow::openProjectDialog)
        ->setShortcut(QKeySequence::Open);
    fileMenu->addAction(tr("&Save project"), this, &MainWindow::saveProject)
        ->setShortcut(QKeySequence::Save);
    fileMenu->addAction(tr("Save project &as..."), this, &MainWindow::saveProjectAs)
        ->setShortcut(QKeySequence::SaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit)
        ->setShortcut(QKeySequence::Quit);

    // --- Import ---------------------------------------------------------------
    QMenu *importMenu = menuBar()->addMenu(tr("&Import"));
    importMenu->addAction(tr("Import ADV &files..."), this, &MainWindow::importFiles);

    // --- Export ---------------------------------------------------------------
    QMenu *exportMenu = menuBar()->addMenu(tr("&Export"));
    QMenu *dataMenu = exportMenu->addMenu(tr("&Data"));
    dataMenu->addAction(tr("Shown series as &CSV..."), this, &MainWindow::exportCsv);
    dataMenu->addAction(tr("&Point statistics (xlsx)..."), this, &MainWindow::exportPointStats);
    dataMenu->addAction(tr("Profile statistics (&template xlsx)..."),
                        this, &MainWindow::exportProfileStats);
    QMenu *plotsMenu = exportMenu->addMenu(tr("&Plots"));
    plotsMenu->addAction(tr("Current frame as &PNG (300 dpi)..."), this, &MainWindow::exportPng);

    // --- View (plot frames) ---------------------------------------------------
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("&Add plot frame below"), this, &MainWindow::addSecondPlotFrame);
    m_removeFrameAction = viewMenu->addAction(tr("&Remove second plot frame"),
                                              this, &MainWindow::removeSecondPlotFrame);
    m_removeFrameAction->setEnabled(false);

    // --- Processing ---------------------------------------------------------
    QMenu *processingMenu = menuBar()->addMenu(tr("&Processing"));
    auto *cpuWidget = new QWidget(this);
    auto *cpuLayout = new QHBoxLayout(cpuWidget);
    cpuLayout->setContentsMargins(8, 2, 8, 2);
    cpuLayout->addWidget(new QLabel(tr("CPUs for processing:"), cpuWidget));
    auto *cpuSpin = new QSpinBox(cpuWidget);
    cpuSpin->setRange(1, ProjectModel::maxCpuCount());
    cpuSpin->setValue(m_model->cpuCount());
    connect(cpuSpin, &QSpinBox::valueChanged, m_model, &ProjectModel::setCpuCount);
    cpuLayout->addWidget(cpuSpin);
    auto *cpuAction = new QWidgetAction(this);
    cpuAction->setDefaultWidget(cpuWidget);
    processingMenu->addAction(cpuAction);
    processingMenu->addSeparator();

    auto *wGroup = new QActionGroup(this);
    QAction *w1Action = processingMenu->addAction(tr("Use w1 for W statistics"));
    QAction *w2Action = processingMenu->addAction(tr("Use w2 for W statistics"));
    for (QAction *action : {w1Action, w2Action}) {
        action->setCheckable(true);
        wGroup->addAction(action);
    }
    w1Action->setChecked(true);
    connect(w1Action, &QAction::triggered, this, [this]() { m_model->setWRole(Role::W1); });
    connect(w2Action, &QAction::triggered, this, [this]() { m_model->setWRole(Role::W2); });

    // --- About ---------------------------------------------------------------
    QMenu *aboutMenu = menuBar()->addMenu(tr("&About"));
    aboutMenu->addAction(tr("&About ADV-Explorer..."), this, &MainWindow::showAbout);
}

void MainWindow::createPointAt(double x, double y)
{
    PointWizard wizard(x, y, this);
    if (wizard.exec() == QDialog::Accepted)
        m_model->addPoint(wizard.result());
}

void MainWindow::editPoint(const QUuid &pointId)
{
    const MeasurementPoint *point = m_model->point(pointId);
    if (!point)
        return;
    PointWizard wizard(*point, this);
    if (wizard.exec() != QDialog::Accepted)
        return;
    if (wizard.deleteRequested())
        m_model->removePoint(pointId);
    else
        m_model->updatePoint(wizard.result());
}

void MainWindow::importFiles()
{
    ImportWizard wizard(this);
    if (wizard.exec() != QDialog::Accepted)
        return;
    for (const MeasurementPoint &point : wizard.results())
        m_model->addPoint(point);
    statusBar()->showMessage(tr("Imported %1 measurement points.").arg(wizard.results().size()));
}

void MainWindow::newProject()
{
    m_model->clear();
    m_projectPath.clear();
    setWindowTitle(QStringLiteral("ADV-Explorer"));
}

void MainWindow::openProjectDialog()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Open project"), QString(), project::fileFilter());
    if (!filePath.isEmpty())
        openProject(filePath);
}

void MainWindow::openProject(const QString &filePath)
{
    QString error;
    if (!project::load(m_model, filePath, &error)) {
        QMessageBox::critical(this, tr("Open project"), error);
        return;
    }
    m_projectPath = filePath;
    setWindowTitle(QStringLiteral("ADV-Explorer - %1").arg(QFileInfo(filePath).fileName()));
    applyPlotSettings();
    statusBar()->showMessage(tr("Loaded %1 measurement points.").arg(m_model->points().size()));
}

bool MainWindow::saveProject()
{
    if (m_projectPath.isEmpty())
        return saveProjectAs();
    return writeProject(m_projectPath);
}

bool MainWindow::saveProjectAs()
{
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Save project"), QString(), project::fileFilter());
    if (filePath.isEmpty())
        return false;
    if (!filePath.endsWith(QStringLiteral(".advProj")))
        filePath += QStringLiteral(".advProj");
    return writeProject(filePath);
}

bool MainWindow::writeProject(const QString &filePath)
{
    collectPlotSettings();
    QString error;
    if (!project::save(*m_model, filePath, &error)) {
        QMessageBox::critical(this, tr("Save project"), error);
        return false;
    }
    m_projectPath = filePath;
    setWindowTitle(QStringLiteral("ADV-Explorer - %1").arg(QFileInfo(filePath).fileName()));
    statusBar()->showMessage(tr("Project saved to %1").arg(filePath));
    return true;
}

void MainWindow::collectPlotSettings()
{
    QJsonObject settings;
    QJsonArray frames;
    for (PlotFrame *frame : m_plotFrames)
        frames.append(frame->saveState());
    settings[QStringLiteral("plotFrames")] = frames;
    settings[QStringLiteral("profileFrame")] = m_profileFrame->saveState();
    settings[QStringLiteral("flumeLength")] = m_flumeView->flumeLength();
    settings[QStringLiteral("flumeWidth")] = m_flumeView->flumeWidth();
    m_model->setPlotSettings(settings);
}

void MainWindow::applyPlotSettings()
{
    const QJsonObject settings = m_model->plotSettings();
    if (settings.isEmpty())
        return;
    m_flumeView->setFlumeSize(settings[QStringLiteral("flumeLength")].toDouble(5.0),
                              settings[QStringLiteral("flumeWidth")].toDouble(1.0));
    const QJsonArray frames = settings[QStringLiteral("plotFrames")].toArray();
    while (m_plotFrames.size() < frames.size() && m_plotFrames.size() < 2)
        addSecondPlotFrame();
    for (int i = 0; i < m_plotFrames.size() && i < frames.size(); ++i)
        m_plotFrames[i]->restoreState(frames.at(i).toObject());
    m_profileFrame->restoreState(settings[QStringLiteral("profileFrame")].toObject());
}

void MainWindow::addSecondPlotFrame()
{
    if (m_plotFrames.size() >= 2)
        return;
    auto *frame = new PlotFrame(m_model, this);
    m_plotFrames.append(frame);
    m_plotColumn->addWidget(frame);
    m_removeFrameAction->setEnabled(true);
}

void MainWindow::removeSecondPlotFrame()
{
    if (m_plotFrames.size() < 2)
        return;
    PlotFrame *frame = m_plotFrames.takeLast();
    m_plotColumn->removeWidget(frame);
    frame->deleteLater();
    m_removeFrameAction->setEnabled(false);
}

void MainWindow::exportCsv()
{
    QList<PlotFrame::ExportSeries> allSeries;
    for (PlotFrame *frame : m_plotFrames)
        allSeries += frame->currentSeries();
    if (allSeries.isEmpty()) {
        QMessageBox::information(this, tr("Export CSV"),
                                 tr("No data series are currently shown."));
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export shown data as CSV"), QString(), tr("CSV files (*.csv)"));
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export CSV"), file.errorString());
        return;
    }
    QTextStream out(&file);
    // header: one time and one value column per series
    QStringList header;
    int maxRows = 0;
    for (const auto &series : allSeries) {
        header << QStringLiteral("t (s) [%1]").arg(series.label)
               << series.label;
        maxRows = std::max(maxRows, int(series.time.size()));
    }
    out << header.join(QLatin1Char(',')) << '\n';
    for (int row = 0; row < maxRows; ++row) {
        QStringList cells;
        for (const auto &series : allSeries) {
            if (row < series.time.size()) {
                cells << QString::number(series.time.at(row), 'g', 10)
                      << QString::number(series.values.at(row), 'g', 10);
            } else {
                cells << QString() << QString();
            }
        }
        out << cells.join(QLatin1Char(',')) << '\n';
    }
    statusBar()->showMessage(tr("Exported %1 series to %2").arg(allSeries.size()).arg(filePath));
}

void MainWindow::exportPng()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export current frame as PNG (300 dpi)"), QString(), tr("PNG images (*.png)"));
    if (filePath.isEmpty())
        return;
    bool ok = false;
    if (m_tabs->currentIndex() == 1) {
        ok = m_profileFrame->exportPng(filePath);
    } else {
        // when two frames are stacked, export both side by side is not
        // supported; export the frame the user interacted with last (first
        // frame by default) and a "-2" suffixed file for the second frame
        ok = m_plotFrames.first()->exportPng(filePath);
        if (ok && m_plotFrames.size() > 1) {
            QString second = filePath;
            second.replace(QStringLiteral(".png"), QStringLiteral("-2.png"));
            ok = m_plotFrames.at(1)->exportPng(second);
        }
    }
    if (!ok)
        QMessageBox::critical(this, tr("Export PNG"), tr("Could not write %1").arg(filePath));
    else
        statusBar()->showMessage(tr("Plot exported to %1").arg(filePath));
}

void MainWindow::exportPointStats()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export point statistics"), QString(), tr("Excel workbook (*.xlsx)"));
    if (filePath.isEmpty())
        return;
    QString error;
    if (!statsexport::writePointStats(*m_model, filePath, &error))
        QMessageBox::critical(this, tr("Export statistics"), error);
    else
        statusBar()->showMessage(tr("Point statistics exported to %1").arg(filePath));
}

void MainWindow::exportProfileStats()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export profile statistics (template)"), QString(),
        tr("Excel workbook (*.xlsx)"));
    if (filePath.isEmpty())
        return;
    // look for the template next to the executable and in the source tree
    QString templatePath;
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/templates/ADV-profiles.xlsx"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../templates/ADV-profiles.xlsx"),
        QStringLiteral("templates/ADV-profiles.xlsx"),
    };
    for (const QString &candidate : candidates) {
        if (QFile::exists(candidate)) {
            templatePath = candidate;
            break;
        }
    }
    QString error;
    if (!statsexport::fillProfileTemplate(*m_model, templatePath, filePath, &error))
        QMessageBox::critical(this, tr("Export statistics"), error);
    else
        statusBar()->showMessage(tr("Profile statistics exported to %1").arg(filePath));
}

void MainWindow::showAbout()
{
    AboutDialog dialog(this);
    dialog.exec();
}

bool MainWindow::captureDocScreenshots(const QString &outputDir)
{
    if (!QDir().mkpath(outputDir))
        return false;

    // demo vertical profile from the u-v-w tables shipped in input-data/;
    // the u component is scaled per depth to a log-law-like shape so the
    // profile view is illustrative (screenshots only, never analysis output)
    auto addCsvPoint = [this](const QString &filePath, double x, double y,
                              double z, double depth, double uScale) -> QUuid {
        QHash<Role, int> mapping;
        mapping.insert(Role::U, 0);
        mapping.insert(Role::V, 1);
        mapping.insert(Role::W1, 2);
        QString error;
        MeasurementPoint point;
        point.data = CsvReader::readFile(filePath, mapping, &error);
        if (point.data.isEmpty())
            return QUuid();
        point.data.synthesizeTime(200.0);
        const int uColumn = point.data.columnOfRole(Role::U);
        if (uColumn >= 0 && uScale != 1.0) {
            for (double &u : point.data.column(uColumn))
                u *= uScale;
        }
        point.x = x;
        point.y = y;
        point.z = z;
        point.waterDepth = depth;
        point.despike.velEnabled = true;
        return m_model->addPoint(point);
    };

    QList<QUuid> profileIds;
    for (int i = 1; i <= 5; ++i) {
        const double z = 0.05 * i;
        const double uScale = 0.6 + 0.4 * std::pow(z / 0.30, 0.4);
        const QUuid id = addCsvPoint(QStringLiteral("input-data/vel%1.dat").arg(i),
                                     0.5, 0.0, z, 0.30, uScale);
        if (!id.isNull())
            profileIds.append(id);
    }
    addCsvPoint(QStringLiteral("input-data/vel7.dat"), 1.5, 0.2, 0.10, 0.28, 1.0);
    if (profileIds.size() < 2)
        return false;

    // preconfigure plots: two velocity series plus the TKE series
    auto makeSeries = [](const QUuid &id, const QString &column, const QString &color) {
        QJsonObject style;
        style[QStringLiteral("lineColor")] = color;
        style[QStringLiteral("markerColor")] = color;
        QJsonObject series;
        series[QStringLiteral("pointId")] = id.toString(QUuid::WithoutBraces);
        series[QStringLiteral("column")] = column;
        series[QStringLiteral("style")] = style;
        return series;
    };
    QJsonArray series;
    series.append(makeSeries(profileIds.at(0), QStringLiteral("u (m/s)"),
                             QStringLiteral("#0072B2")));
    series.append(makeSeries(profileIds.at(0), QStringLiteral("w1 (m/s)"),
                             QStringLiteral("#009E73")));
    series.append(makeSeries(profileIds.at(3), QStringLiteral("u (m/s)"),
                             QStringLiteral("#E69F00")));
    QJsonObject frame;
    frame[QStringLiteral("palette")] = 0;
    frame[QStringLiteral("series")] = series;
    QJsonArray frames;
    frames.append(frame);

    QJsonObject profileState;
    profileState[QStringLiteral("profile")] = MeasurementPoint::makeXyKey(0.5, 0.0);
    profileState[QStringLiteral("u")] = true;
    profileState[QStringLiteral("v")] = true;
    profileState[QStringLiteral("w")] = true;
    profileState[QStringLiteral("relative")] = false;

    QJsonObject settings;
    settings[QStringLiteral("plotFrames")] = frames;
    settings[QStringLiteral("profileFrame")] = profileState;
    settings[QStringLiteral("flumeLength")] = 2.0;
    settings[QStringLiteral("flumeWidth")] = 0.4;
    m_model->setPlotSettings(settings);
    applyPlotSettings();

    auto snap = [this, &outputDir](QWidget *widget, const QString &name) {
        QCoreApplication::processEvents();
        return widget->grab().save(outputDir + QLatin1Char('/') + name);
    };

    m_tabs->setCurrentIndex(0);
    bool ok = snap(this, QStringLiteral("main-window.png"));

    m_tabs->setCurrentIndex(1);
    ok = snap(this, QStringLiteral("vertical-profiles.png")) && ok;
    m_tabs->setCurrentIndex(0);

    PointWizard wizard(0.5, 0.0, this);
    wizard.show();
    ok = snap(&wizard, QStringLiteral("point-wizard.png")) && ok;
    wizard.close();

    return ok;
}
