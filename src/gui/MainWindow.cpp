/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "MainWindow.h"

#include "AboutDialog.h"
#include "CrsDialog.h"
#include "FlumeView.h"
#include "FlowTrackerImportWizard.h"
#include "GuidedTour.h"
#include "ImportWizard.h"
#include "MapView.h"
#include "PlotFrame.h"
#include "PointWizard.h"
#include "ProfileFrame.h"

#include "core/Crs.h"
#include "core/CsvReader.h"
#include "core/ExampleProject.h"
#include "core/ProfileStatsExport.h"
#include "core/Project.h"
#include "core/ProjectModel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScreen>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>

using namespace adv;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_model(new ProjectModel(this))
{
    setWindowTitle(QStringLiteral("ADV-Explorer"));
    // The preferred size does not fit every display: 860 px of height overflows
    // a 1366x768 laptop, which is a very ordinary field machine, and a window
    // taller than the screen hides the status bar with no easy way to reach it.
    // Clamp to what the screen actually offers, leaving room for the task bar.
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QSize available = screen->availableGeometry().size();
        resize(qMin(1280, available.width() - 40), qMin(860, available.height() - 60));
    } else {
        resize(1280, 860);
    }

    // subtle background image for a modern app look
    setStyleSheet(QStringLiteral(
        "QMainWindow { background-image: url(:/img/app-background.jpg);"
        " background-position: center; }"));


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

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(new QWidget(m_splitter)); // replaced by applyMode()
    m_splitter->addWidget(m_tabs);
    // site view gets 2/5 of the height (20% more than the earlier 1/3)
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 3);
    // Neither pane may be collapsed away. The proportions below are applied on
    // the first show, but a splitter is free to squeeze a collapsible child to
    // nothing, and a window without its flume or map is not a usable window.
    m_splitter->setChildrenCollapsible(false);
    setCentralWidget(m_splitter);

    buildMenus();
    applyMode(m_model->mode());
    connect(m_model, &ProjectModel::modeChanged, this, &MainWindow::applyMode);
    statusBar()->showMessage(tr("Click into the flume to define a measurement point."));
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_splitProportioned)
        return;
    m_splitProportioned = true;
    // Only now does the splitter have a height to divide. Doing this in the
    // constructor divided the window height instead, before any layout had run.
    applySplitterProportions();
}

bool MainWindow::siteViewIsUsable() const
{
    // Releases 0.2.0 to 0.2.2 all shipped with no flume and no map, each time
    // because the site view ended up with no space in the splitter. Checking its
    // own height() is not enough: a widget that never made it into the layout
    // still reports the geometry it was born with. What matters is that it sits
    // in the splitter, that it is visible, and that the splitter gave it room.
    if (!m_siteView) {
        qWarning("no site view exists");
        return false;
    }
    const int index = m_splitter->indexOf(m_siteView);
    const int pane = m_splitter->sizes().value(0);
    if (index != 0 || !m_siteView->isVisible() || pane < 50) {
        qWarning("site view not usable: splitter index %d (want 0), visible %d, "
                 "pane height %d (want >= 50). The flume or the map would be "
                 "missing from the window.",
                 index, int(m_siteView->isVisible()), pane);
        return false;
    }
    return true;
}

void MainWindow::applySplitterProportions()
{
    const int total = m_splitter->height();
    if (total <= 0) {
        // layout has still not run; try again once the event loop has caught up
        QTimer::singleShot(0, this, &MainWindow::applySplitterProportions);
        return;
    }
    m_splitter->setSizes({2 * total / 5, 3 * total / 5});
}

void MainWindow::buildMenus()
{
    // --- File ---------------------------------------------------------------
    // note: the addAction(text, shortcut, receiver, slot) convenience overload
    // only exists since Qt 6.3; set shortcuts explicitly for Qt 6.2 support
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu = fileMenu;
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
    m_importMenu = importMenu;
    importMenu->addAction(tr("Import ADV &files..."), this, &MainWindow::importFiles);
    m_importFtAction = importMenu->addAction(tr("Import FlowTracker2 &survey..."),
                                             this, &MainWindow::importFlowTrackerSurvey);

    // --- Project (campaign mode and coordinate system) ------------------------
    // mode belongs here rather than in a menu of its own: it is a property of
    // the project and is saved with it
    QMenu *projectMenu = menuBar()->addMenu(tr("&Project"));
    m_projectMenu = projectMenu;
    auto *modeGroup = new QActionGroup(this);
    m_labModeAction = projectMenu->addAction(tr("&Lab mode (Vectrino, flume)"));
    m_fieldModeAction = projectMenu->addAction(tr("&Field mode (FlowTracker, river)"));
    for (QAction *action : {m_labModeAction, m_fieldModeAction}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    m_labModeAction->setChecked(true);
    connect(m_labModeAction, &QAction::triggered, this, [this]() { m_model->setMode(Mode::Lab); });
    connect(m_fieldModeAction, &QAction::triggered, this, [this]() {
        if (!m_model->points().isEmpty()) {
            const auto answer = QMessageBox::question(
                this, tr("Switch to field mode"),
                tr("The %1 existing measurement points keep their x and y values, but "
                   "those values will now be read as easting and northing in the project "
                   "coordinate system.\n\nNo coordinates are converted. Continue?")
                    .arg(m_model->points().size()));
            if (answer != QMessageBox::Yes) {
                m_labModeAction->setChecked(true);
                return;
            }
        }
        m_model->setMode(Mode::Field);
        if (m_model->epsg() == 0)
            chooseProjectCrs();
    });
    projectMenu->addSeparator();
    m_crsAction = projectMenu->addAction(tr("&Coordinate system..."),
                                         this, &MainWindow::chooseProjectCrs);

    // --- Export ---------------------------------------------------------------
    QMenu *exportMenu = menuBar()->addMenu(tr("&Export"));
    m_exportMenu = exportMenu;
    QMenu *dataMenu = exportMenu->addMenu(tr("&Data"));
    dataMenu->addAction(tr("Shown series as &CSV..."), this, &MainWindow::exportCsv);
    dataMenu->addAction(tr("&Point statistics (xlsx)..."), this, &MainWindow::exportPointStats);
    dataMenu->addAction(tr("Profile statistics (&template xlsx)..."),
                        this, &MainWindow::exportProfileStats);
    QMenu *plotsMenu = exportMenu->addMenu(tr("&Plots"));
    plotsMenu->addAction(tr("Current frame as &PNG (300 dpi)..."), this, &MainWindow::exportPng);
    m_exportMapAction = plotsMenu->addAction(tr("&Map view as PNG (300 dpi)..."),
                                             this, &MainWindow::exportMapPng);

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
    m_w2Action = w2Action;
    for (QAction *action : {w1Action, w2Action}) {
        action->setCheckable(true);
        wGroup->addAction(action);
    }
    w1Action->setChecked(true);
    connect(w1Action, &QAction::triggered, this, [this]() { m_model->setWRole(Role::W1); });
    connect(w2Action, &QAction::triggered, this, [this]() { m_model->setWRole(Role::W2); });

    // --- Help ----------------------------------------------------------------
    // About lives here rather than in a menu of its own, which is where users
    // look for it and keeps two near-identical menus from sitting side by side.
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Online &documentation..."), this, &MainWindow::openDocumentation);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Load example: &Lab (Vectrino)"), this,
                        &MainWindow::loadLabExample);
    helpMenu->addAction(tr("Load example: &Field (FlowTracker)"), this,
                        &MainWindow::loadFieldExample);
    helpMenu->addAction(tr("&Restart guided tour"), this, &MainWindow::startGuidedTour);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("&About ADV-Explorer..."), this, &MainWindow::showAbout);
}

void MainWindow::applyMode(Mode mode)
{
    // remember what the outgoing view was showing before it is destroyed
    if (m_siteView) {
        QJsonObject settings = m_model->plotSettings();
        settings[m_model->mode() == Mode::Field ? QStringLiteral("mapView")
                                                : QStringLiteral("flumeView")] =
            m_siteView->saveState();
        m_model->setPlotSettings(settings);
    }

    SiteView *view = mode == Mode::Field
                         ? static_cast<SiteView *>(new MapView(m_model, this))
                         : static_cast<SiteView *>(new FlumeView(m_model, this));
    connect(view, &SiteView::newPointRequested, this, &MainWindow::createPointAt);
    connect(view, &SiteView::editPointRequested, this, &MainWindow::editPoint);

    // replaceWidget keeps the splitter proportions the user set
    QWidget *old = m_splitter->replaceWidget(0, view);
    if (old) {
        old->deleteLater();
    } else {
        // it refuses in some situations and then the view is in no layout at
        // all; put it in the splitter by hand rather than lose it
        m_splitter->insertWidget(0, view);
    }
    // A splitter gives a hidden child zero size and no handle, and not every Qt
    // version shows the widget that replaceWidget put in. That is precisely how
    // 0.2.0 to 0.2.2 shipped with no flume and no map, so do not rely on it.
    view->show();
    m_splitter->setStretchFactor(0, 2);
    m_siteView = view;

    const QJsonObject settings = m_model->plotSettings();
    const QString key = mode == Mode::Field ? QStringLiteral("mapView")
                                            : QStringLiteral("flumeView");
    if (settings.contains(key))
        view->restoreState(settings[key].toObject());
    view->rebuild();

    if (m_labModeAction)
        m_labModeAction->setChecked(mode == Mode::Lab);
    if (m_fieldModeAction)
        m_fieldModeAction->setChecked(mode == Mode::Field);
    if (m_crsAction)
        m_crsAction->setEnabled(mode == Mode::Field);
    if (m_importFtAction)
        m_importFtAction->setEnabled(mode == Mode::Field);
    if (m_exportMapAction)
        m_exportMapAction->setEnabled(mode == Mode::Field);
    if (m_w2Action) {
        // a FlowTracker2 probe has three beams and a single vertical component
        m_w2Action->setEnabled(mode == Mode::Lab);
        m_w2Action->setToolTip(mode == Mode::Field
                                   ? tr("FlowTracker2 measures a single vertical component.")
                                   : QString());
    }

    statusBar()->showMessage(mode == Mode::Field
                                 ? tr("Click the map to define a measurement point.")
                                 : tr("Click into the flume to define a measurement point."));
}

void MainWindow::exportMapPng()
{
    auto *map = qobject_cast<MapView *>(m_siteView);
    if (!map) {
        QMessageBox::information(this, tr("Export map"),
                                 tr("The map view is only available in field mode."));
        return;
    }
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("Export the map view as PNG (300 dpi)"), QString(), tr("PNG images (*.png)"));
    if (filePath.isEmpty())
        return;

    // render at three times the on-screen size so 300 dpi output stays legible;
    // the OpenStreetMap attribution is painted by paintEvent and is therefore
    // part of the image, which the licence requires
    const QImage image = map->renderImage(map->size() * 3, 300);
    if (!image.save(filePath))
        QMessageBox::critical(this, tr("Export map"), tr("Could not write %1").arg(filePath));
    else
        statusBar()->showMessage(tr("Map exported to %1").arg(filePath));
}

void MainWindow::chooseProjectCrs()
{
    CrsDialog dialog(m_model->epsg(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (!m_model->setEpsg(dialog.selectedEpsg())) {
        QMessageBox::warning(this, tr("Coordinate system"),
                             tr("EPSG:%1 is not supported. %2")
                                 .arg(dialog.selectedEpsg())
                                 .arg(crs::supportedRangesText()));
        return;
    }
    statusBar()->showMessage(tr("Project coordinate system: EPSG:%1 (%2).")
                                 .arg(m_model->epsg())
                                 .arg(crs::name(m_model->epsg())));
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

void MainWindow::importFlowTrackerSurvey()
{
    FlowTrackerImportWizard wizard(m_model, this);
    if (wizard.exec() != QDialog::Accepted)
        return;
    for (const CrossSection &section : wizard.crossSections())
        m_model->addCrossSection(section);
    for (const MeasurementPoint &point : wizard.results())
        m_model->addPoint(point);
    statusBar()->showMessage(tr("Imported %1 measurement points from %2 stations.")
                                 .arg(wizard.results().size())
                                 .arg(wizard.stationCount()));
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
    QString warning;
    if (!project::load(m_model, filePath, &error, &warning)) {
        QMessageBox::critical(this, tr("Open project"), error);
        return;
    }
    if (!warning.isEmpty())
        QMessageBox::warning(this, tr("Open project"), warning);
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
    // on Windows the native dialog may hand back the suffix in another case,
    // and appending a second one would produce "survey.ADVPROJ.advProj"
    if (!filePath.endsWith(QStringLiteral(".advProj"), Qt::CaseInsensitive))
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
    if (m_siteView) {
        settings[m_model->mode() == Mode::Field ? QStringLiteral("mapView")
                                                : QStringLiteral("flumeView")] =
            m_siteView->saveState();
    }
    // keep whichever view is not currently shown from losing its state
    const QJsonObject previous = m_model->plotSettings();
    for (const QString &key : {QStringLiteral("mapView"), QStringLiteral("flumeView")}) {
        if (!settings.contains(key) && previous.contains(key))
            settings[key] = previous[key];
    }
    m_model->setPlotSettings(settings);
}

void MainWindow::applyPlotSettings()
{
    const QJsonObject settings = m_model->plotSettings();
    if (settings.isEmpty())
        return;
    if (m_siteView) {
        const QString key = m_model->mode() == Mode::Field ? QStringLiteral("mapView")
                                                           : QStringLiteral("flumeView");
        if (settings.contains(key)) {
            m_siteView->restoreState(settings[key].toObject());
        } else {
            // projects written before the views got their own state blocks
            QJsonObject legacy;
            legacy[QStringLiteral("length")] = settings[QStringLiteral("flumeLength")].toDouble(5.0);
            legacy[QStringLiteral("width")] = settings[QStringLiteral("flumeWidth")].toDouble(1.0);
            m_siteView->restoreState(legacy);
        }
    }
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

void MainWindow::openDocumentation()
{
    const QUrl url(QStringLiteral("https://adv-explorer.readthedocs.io/"));
    if (!QDesktopServices::openUrl(url)) {
        QMessageBox::information(
            this, tr("Online documentation"),
            tr("No web browser could be started. The documentation is at:\n\n%1")
                .arg(url.toString()));
    }
}

bool MainWindow::confirmReplaceProject()
{
    if (m_model->points().isEmpty())
        return true;
    const auto answer = QMessageBox::question(
        this, tr("Load example"),
        tr("Loading an example replaces the current project, including its %1 "
           "measurement points. Any unsaved changes are lost.\n\nContinue?")
            .arg(m_model->points().size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer == QMessageBox::Yes;
}

void MainWindow::loadLabExample()
{
    loadExample(Mode::Lab);
}

void MainWindow::loadFieldExample()
{
    loadExample(Mode::Field);
}

void MainWindow::loadExample(Mode mode)
{
    if (!confirmReplaceProject())
        return;

    QString error;
    const bool ok = mode == Mode::Field ? examples::loadField(m_model, &error)
                                        : examples::loadLab(m_model, &error);
    if (!ok) {
        QMessageBox::critical(this, tr("Load example"),
                              error.isEmpty() ? tr("The example could not be loaded.")
                                              : error);
        return;
    }

    m_projectPath.clear();
    setWindowTitle(mode == Mode::Field
                       ? tr("ADV-Explorer - Example: field (FlowTracker)")
                       : tr("ADV-Explorer - Example: laboratory (Vectrino)"));
    applyPlotSettings();
    statusBar()->showMessage(
        tr("Example loaded: %1 measurement points. Save it under File > Save "
           "project as to keep your changes.")
            .arg(m_model->points().size()));

    startGuidedTour();
}

void MainWindow::startGuidedTour()
{
    if (!m_tour) {
        m_tour = new GuidedTour(this);
        connect(m_tour, &GuidedTour::tabRequested, this, [this](int index) {
            if (index >= 0 && index < m_tabs->count())
                m_tabs->setCurrentIndex(index);
        });
        addDockWidget(Qt::RightDockWidgetArea, m_tour);

        // the dock asks for width of its own, which on a small display would
        // push the window past the screen edge; take the space from the views
        // instead, exactly as the constructor does for the initial size
        if (const QScreen *screen = QGuiApplication::primaryScreen()) {
            const QSize available = screen->availableGeometry().size();
            resize(qMin(width(), available.width() - 40),
                   qMin(height(), available.height() - 60));
        }
    }
    configureTour();
    m_tour->start();
}

void MainWindow::configureTour()
{
    if (!m_tour)
        return;

    const bool field = m_model->mode() == Mode::Field;
    PlotFrame *plotFrame = m_plotFrames.isEmpty() ? nullptr : m_plotFrames.first();
    QWidget *plotArea = plotFrame ? static_cast<QWidget *>(plotFrame)
                                  : static_cast<QWidget *>(m_tabs);

    // The tour walks the actual workflow, so every step points at the control
    // the user would click next. The controls carry object names for exactly
    // this; a missing one simply leaves that step pointing at its frame.
    auto control = [](QWidget *parent, const char *name) -> QWidget * {
        return parent ? parent->findChild<QWidget *>(QLatin1String(name)) : nullptr;
    };
    auto menuTitle = [this](QMenu *menu) {
        return menu ? menuBar()->actionGeometry(menu->menuAction()) : QRect();
    };

    QList<GuidedTour::Step> steps;
    auto step = [&steps](const QString &title, const QString &body, QWidget *target,
                         int tab = -1, const QList<QPointer<QWidget>> &extras = {},
                         const QRect &rect = QRect()) {
        GuidedTour::Step s;
        s.title = title;
        s.body = body;
        s.target = target;
        s.extras = extras;
        s.rect = rect;
        s.tabIndex = tab;
        steps.append(s);
    };

    // --- overview -------------------------------------------------------------
    step(tr("The workflow"),
         tr("<p>This tour follows the order you actually work in:</p>"
            "<ol><li>bring measurements in</li>"
            "<li>set the frame of reference</li>"
            "<li>describe every point</li>"
            "<li>clean the signal</li>"
            "<li>plot the time series</li>"
            "<li>read the vertical profile</li>"
            "<li>check the statistics</li>"
            "<li>correct the probe alignment</li>"
            "<li>export</li>"
            "<li>save the project</li></ol>"
            "<p>Each step frames the control it talks about. Nothing here blocks "
            "the window: click along on the loaded example as you read, and use "
            "<i>&lt; Back</i> if you want to see a step again.</p>"),
         nullptr);

    // --- 1. import ------------------------------------------------------------
    step(tr("1. Bring measurements in"),
         field ? tr("<p>Use <i>Import &gt; Import FlowTracker2 survey...</i>. One "
                    "survey file holds a whole cross section, so this reads every "
                    "vertical at once. Preferred is the instrument's own "
                    "<b>.ft</b> archive; the <b>.ft.dat.csv</b> export works as a "
                    "fallback with fewer columns.</p>"
                    "<p>The wizard shows the stations it found and asks how to "
                    "place them: type the coordinates of the two tape ends, or "
                    "load surveyed positions from a GeoPackage or CSV. It then "
                    "creates one measurement point per depth of every station.</p>")
               : tr("<p>Use <i>Import &gt; Import ADV files...</i> for a batch. A "
                    "file dialog opens first: select all files of the campaign "
                    "(<b>.vna</b>, <b>.csv</b>, <b>.txt</b>, <b>.dat</b>), then "
                    "give each row its x, y and z in metres, plus the water depth "
                    "if you know it.</p>"
                    "<p>File names of the form <b>50_10_15_run.vna</b> pre-fill the "
                    "coordinates in centimetres, where a leading <b>__</b> means a "
                    "negative value. For a single file it is quicker to click into "
                    "the flume, which opens the same point editor.</p>"),
         menuBar(), -1, {}, menuTitle(m_importMenu));

    // --- 2. frame of reference ------------------------------------------------
    if (field) {
        step(tr("2. Set the coordinate system"),
             tr("<p>Point x and y are <b>easting and northing</b>, so the project "
                "needs a coordinate system: <i>Project &gt; Coordinate system</i>. "
                "The example uses ETRS89 / UTM zone 32N (EPSG:25832). WGS 84 and "
                "ETRS89 UTM, Pseudo-Mercator and Gauss-Krueger are built in, so no "
                "PROJ or GDAL installation is needed.</p>"
                "<p>The z coordinate keeps the meaning it has in the flume: height "
                "above the bed, in metres. That is what makes the vertical profile "
                "and the z/h axis work in the field as well.</p>"),
             menuBar(), -1, {}, menuTitle(m_projectMenu));
        step(tr("Reading the map"),
             tr("<p>Each marker is one <b>vertical</b> and its badge counts the "
                "depths measured there; the dashed line is the surveyed cross "
                "section. Drag to pan, use the wheel to zoom.</p>"
                "<p>Turn <i>Online basemap</i> off to work without a connection. "
                "The coordinate grid, the scale bar and the cursor readout keep "
                "the view quantitative either way, and both are painted into an "
                "exported PNG.</p>"),
             m_siteView, -1,
             {control(m_siteView, "mapControlBar")});
    } else {
        step(tr("2. Set the flume geometry"),
             tr("<p>Enter the real <b>length and width</b> of your flume here. The "
                "drawing always fills the panel, so it is stretched independently "
                "in x and y; clicked positions and markers nevertheless always map "
                "to the metres you type in.</p>"
                "<p>The origin, drawn as a red cross, sits at the centre of the "
                "inlet: x runs downstream, y towards the right bank (the left bank "
                "is negative), z upward from the bed.</p>"),
             control(m_siteView, "flumeLengthSpin") ? control(m_siteView, "flumeLengthSpin")
                                                    : m_siteView,
             -1, {control(m_siteView, "flumeWidthSpin")});
    }

    // --- 3. point attributes --------------------------------------------------
    step(tr("3. Describe every point"),
         field ? tr("<p>Click a marker to open the <b>point editor</b>. It holds "
                    "the position, the water depth of the vertical, the data of "
                    "that depth, an optional analysis time window and the "
                    "despiking filters.</p>"
                    "<p>The water depth is what the relative z/h axis and the "
                    "profile statistics need; the import fills it in from the "
                    "station depth the instrument recorded. Editing it at one "
                    "point applies it to every point of that vertical.</p>")
               : tr("<p>Click a marker to open the <b>point editor</b>, or click "
                    "anywhere in the water to place a new point. Set:</p>"
                    "<ul><li>x, y, z in metres,</li>"
                    "<li>the water depth h, which applies to every point of that "
                    "x-y position and is what the z/h axis needs,</li>"
                    "<li>the data file, with a mapping table for its columns,</li>"
                    "<li>the sampling frequency, used when the file has no time "
                    "column,</li>"
                    "<li>an optional time window that restricts every statistic "
                    "and plot of this point.</li></ul>"),
         m_siteView, -1);

    // --- 4. despiking ---------------------------------------------------------
    step(tr("4. Clean the signal"),
         tr("<p>The lower half of the point editor holds the <b>despiking "
            "filters</b>: correlation and SNR thresholds, a velocity threshold at "
            "k standard deviations, the Goring &amp; Nikora method and iterative "
            "phase-space thresholding. Removed samples either stay as gaps or are "
            "filled by linear interpolation. They chain, and the raw file is never "
            "modified.</p>"
            "<p>%1 Under <i>Processing</i> you also choose how many CPU cores the "
            "analysis may use, and whether w1 or w2 is the vertical component.</p>")
             .arg(field ? tr("Field imports start with the correlation filter "
                             "<b>off</b> on purpose: the instrument reports "
                             "correlation on a different scale, where the "
                             "laboratory default of 70 would reject nearly every "
                             "sample. The SNR threshold is taken from the survey "
                             "file itself.")
                        : tr("The example switches the velocity threshold on, "
                             "which drops samples further than three standard "
                             "deviations from the mean.")),
         m_siteView, -1);

    // --- 5. time series -------------------------------------------------------
    step(tr("5. Plot the time series"),
         tr("<p>Pick a <b>point</b>, pick a <b>data series</b>, press <b>Add</b>. "
            "Repeat for as many points as you want to compare: they superpose in "
            "the same frame. <i>Style...</i> changes line and marker appearance, "
            "the palette recolours everything at once in a colour-blind friendly "
            "scheme, and <i>View &gt; Add plot frame below</i> gives you a second, "
            "independent frame.</p>"
            "<p>Besides the measured columns the list holds a derived series, "
            "%1, computed as 0.5 (var U + var V + var W). Drag to pan, use the "
            "wheel to zoom.</p>")
             .arg(field ? tr("<b>TKE proxy</b>") : tr("<b>TKE</b>")),
         control(plotArea, "plotPointCombo") ? control(plotArea, "plotPointCombo") : plotArea,
         0,
         {control(plotArea, "plotColumnCombo"), control(plotArea, "plotAddButton")});

    if (field) {
        step(tr("Why it says \"proxy\""),
             tr("<p>A FlowTracker2 point is roughly 60 samples at 2 Hz over 30 s. "
                "That resolves nothing above 1 Hz, so the variance it yields is "
                "not the turbulent kinetic energy a laboratory record measures.</p>"
                "<p>It stays a useful <i>relative</i> indicator between stations of "
                "one survey, which is why it is computed but labelled as a proxy "
                "everywhere, including in the exported workbooks. Dissipation is "
                "reported as not estimable for the same reason. Longer points, 2 "
                "to 5 minutes, make these numbers far more trustworthy.</p>"),
             plotArea, 0);
    }

    // --- 6. vertical profile --------------------------------------------------
    step(tr("6. Read the vertical profile"),
         tr("<p>Every entry of this list is one <b>vertical</b>, that is all points "
            "sharing an x-y position. The plot shows the mean U, V and W of each "
            "height: U is joined by a line, V and W are drawn as markers only, "
            "because they are near-zero residuals whose sign flips from height to "
            "height.</p>"
            "<p>The check boxes hide components you are not interested in, and the "
            "z / z/h radio buttons switch between absolute height and relative "
            "depth, which needs the water depths of step 3.</p>"),
         control(m_profileFrame, "profileCombo") ? control(m_profileFrame, "profileCombo")
                                                 : m_profileFrame,
         1,
         {control(m_profileFrame, "profileUCheck"), control(m_profileFrame, "profileWCheck"),
          control(m_profileFrame, "profileZRadio"), control(m_profileFrame, "profileZhRadio")});

    // --- 7. statistics --------------------------------------------------------
    step(tr("7. Check the statistics"),
         tr("<p>For every height of the selected vertical this panel lists mean, "
            "standard deviation, skewness and kurtosis of u, v and w, the Reynolds "
            "stresses u'v', u'w' and v'w', the sample count n, %1.</p>"
            "<p>%2</p>")
             .arg(field ? tr("the TKE proxy and the dissipation rate, which a "
                             "30 s point cannot deliver")
                        : tr("the turbulent kinetic energy and the dissipation "
                             "rate from an inertial-subrange fit of the u "
                             "spectrum"),
                  field ? tr("Read U across the heights first. V and W are usually "
                             "much smaller, and near a bank, where the flow is a "
                             "few millimetres per second, no component dominates "
                             "at all. The vertical component is also the noisiest "
                             "one the instrument delivers, so its scatter is "
                             "largely instrument noise.")
                        : tr("Watch n: a filter that removes a large share of the "
                             "samples shows up here first. The dissipation rate "
                             "needs a resolved inertial subrange, so it is only "
                             "meaningful for a fast, long record.")),
         control(m_profileFrame, "profileStatsPanel") ? control(m_profileFrame, "profileStatsPanel")
                                                      : m_profileFrame,
         1);

    // --- 8. probe alignment ---------------------------------------------------
    step(tr("8. Correct the probe alignment"),
         tr("<p>A probe mounted slightly rotated shows a mean V and W that are not "
            "zero even in uniform flow. Press <b>Probe alignment...</b>, then "
            "<i>Propose</i>: the app computes the heading, pitch and roll that "
            "zero the mean transverse and vertical velocities and the residual "
            "v'w' coupling of this vertical.</p>"
            "<p>Accept it or type your own angles. The rotation applies to every "
            "point of the vertical and to every plot, statistic and export that "
            "follows, it can be reset to zero at any time, and it never touches "
            "the raw data.</p>"),
         control(m_profileFrame, "profileAlignButton") ? control(m_profileFrame, "profileAlignButton")
                                                       : m_profileFrame,
         1);

    // --- 9. export ------------------------------------------------------------
    step(tr("9. Export the results"),
         tr("<p>Everything you see can leave the application:</p>"
            "<ul><li><i>Data &gt; Shown series as CSV</i>: the plotted series, "
            "despiked and alignment-corrected exactly as displayed,</li>"
            "<li><i>Data &gt; Point statistics (xlsx)</i>: one row per point,</li>"
            "<li><i>Data &gt; Profile statistics (template xlsx)</i>: per vertical, "
            "written into the bundled template that adds velocity magnitude and "
            "direction,</li>"
            "<li><i>Plots &gt; Current frame as PNG</i>: 300 dpi, as displayed%1."
            "</li></ul>")
             .arg(field ? tr(", and the map with its OpenStreetMap attribution")
                        : QString()),
         menuBar(), -1, {}, menuTitle(m_exportMenu));

    // --- 10. save -------------------------------------------------------------
    step(tr("10. Save the project"),
         tr("<p><i>File &gt; Save project as</i> writes a single <b>.advProj</b> "
            "file that contains the raw measurement data together with every "
            "setting: positions, depths, time windows, filters, alignment angles, "
            "plotted series and their styles.</p>"
            "<p>It is self-contained on purpose, so it opens on a colleague's "
            "computer without any of the original files being present.</p>"),
         menuBar(), -1, {}, menuTitle(m_fileMenu));

    step(tr("That is the workflow"),
         tr("<p>Reopen this tour any time from <i>Help &gt; Restart guided tour</i>, "
            "and load the other example from the same menu to see how the %1 side "
            "works.</p>"
            "<p>The full manual, including the file formats and the definition "
            "behind every statistic, is at "
            "<a href=\"https://adv-explorer.readthedocs.io/\">"
            "adv-explorer.readthedocs.io</a>.</p>")
             .arg(field ? tr("laboratory") : tr("field")),
         nullptr, -1);

    m_tour->setSteps(steps);
}

bool MainWindow::captureDocScreenshots(const QString &outputDir)
{
    if (!QDir().mkpath(outputDir))
        return false;

    // Pin the canonical documentation size. The constructor clamps the window to
    // the screen, and the offscreen platform advertises one of its own, which
    // would otherwise make the published screenshots vary with the build host.
    resize(1280, 860);
    applySplitterProportions();
    QCoreApplication::processEvents();

    if (!siteViewIsUsable())
        return false;

    // The laboratory example is the documentation scene as well, so the two
    // cannot drift apart. It reads from the embedded resources, which also means
    // the screenshot mode no longer depends on being run from the repository
    // root the way it used to.
    QString exampleError;
    if (!examples::loadLab(m_model, &exampleError)) {
        qWarning("cannot build the laboratory example: %s", qPrintable(exampleError));
        return false;
    }
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

    // --- field mode -----------------------------------------------------------
    // The documentation build runs offscreen and without a network, so the
    // basemap is switched off explicitly: what gets captured is the offline
    // fallback, which is also what a user sees in the field without coverage.
    const QList<MeasurementPoint> labPoints = m_model->points();
    m_model->clear();
    m_model->setMode(Mode::Field);
    m_model->setEpsg(25832);

    CrossSection section;
    section.name = QStringLiteral("Isar side channel");
    section.leftChainage = 17.0;
    section.rightChainage = 3.0;
    section.leftX = 677394.935;
    section.leftY = 5268148.607;
    section.rightX = 677403.2;
    section.rightY = 5268137.3;
    for (int i = 0; i <= 14; ++i) {
        const double chainage = 17.0 - i;
        section.bed.append(QPointF(chainage, 0.7 * std::sin(M_PI * i / 14.0)));
    }
    m_model->addCrossSection(section);

    for (int i = 1; i < 14; i += 2) {
        const double chainage = 17.0 - i;
        const double depth = 0.7 * std::sin(M_PI * i / 14.0);
        double x = 0.0;
        double y = 0.0;
        section.positionAt(chainage, &x, &y);
        for (const double fraction : {0.2, 0.6, 0.8}) {
            MeasurementPoint point = labPoints.isEmpty() ? MeasurementPoint()
                                                         : labPoints.first();
            point.id = QUuid::createUuid();
            point.x = x;
            point.y = y;
            point.z = (1.0 - fraction) * depth;
            point.waterDepth = depth;
            point.chainage = chainage;
            point.stationName = QStringLiteral("Isar station %1").arg((i + 1) / 2);
            m_model->addPoint(point);
        }
    }

    QJsonObject mapState;
    mapState[QStringLiteral("basemap")] = false; // no network in the docs build
    mapState[QStringLiteral("zoom")] = 21; // overzoomed: stations sit metres apart
    mapState[QStringLiteral("centerLon")] = 11.35744;
    mapState[QStringLiteral("centerLat")] = 47.54247;
    QJsonObject fieldSettings = m_model->plotSettings();
    fieldSettings[QStringLiteral("mapView")] = mapState;
    m_model->setPlotSettings(fieldSettings);
    applyPlotSettings();

    m_tabs->setCurrentIndex(0);
    QCoreApplication::processEvents();
    if (!siteViewIsUsable()) // the mode switch rebuilds the site view
        return false;
    ok = snap(this, QStringLiteral("field-mode.png")) && ok;

    // --- guided tour ----------------------------------------------------------
    // Shown on the real field example rather than the composed scene above, so
    // the picture is exactly what Help > Load example gives the reader.
    if (!examples::loadField(m_model, &exampleError)) {
        qWarning("cannot build the field example: %s", qPrintable(exampleError));
        return false;
    }
    applyPlotSettings();
    m_tabs->setCurrentIndex(0);
    startGuidedTour();
    // a step with a highlight, so the picture shows what the tour does
    if (m_tour)
        m_tour->start(3);
    QCoreApplication::processEvents();
    ok = snap(this, QStringLiteral("guided-tour.png")) && ok;
    if (m_tour)
        m_tour->close();

    return ok;
}
