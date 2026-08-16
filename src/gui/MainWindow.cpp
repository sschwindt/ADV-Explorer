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
    m_splitter->setSizes({2 * height() / 5, 3 * height() / 5});
    setCentralWidget(m_splitter);

    buildMenus();
    applyMode(m_model->mode());
    connect(m_model, &ProjectModel::modeChanged, this, &MainWindow::applyMode);
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
    m_importFtAction = importMenu->addAction(tr("Import FlowTracker2 &survey..."),
                                             this, &MainWindow::importFlowTrackerSurvey);

    // --- Project (campaign mode and coordinate system) ------------------------
    // mode belongs here rather than in a menu of its own: it is a property of
    // the project and is saved with it
    QMenu *projectMenu = menuBar()->addMenu(tr("&Project"));
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
    if (old)
        old->deleteLater();
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
    QWidget *plotArea = m_plotFrames.isEmpty()
                            ? static_cast<QWidget *>(m_tabs)
                            : static_cast<QWidget *>(m_plotFrames.first());

    QList<GuidedTour::Step> steps;
    auto step = [&steps](const QString &title, const QString &body, QWidget *target,
                         int tab = -1) {
        GuidedTour::Step s;
        s.title = title;
        s.body = body;
        s.target = target;
        s.tabIndex = tab;
        steps.append(s);
    };

    if (field) {
        step(tr("The map"),
             tr("<p>Each marker is a <b>vertical</b> of the cross section, and the "
                "badge counts the measurement depths it holds. The dashed line is "
                "the surveyed cross section itself.</p>"
                "<p>Drag to pan, use the wheel to zoom. Turn <i>Online basemap</i> "
                "off to work without a connection: the coordinate grid and the "
                "scale bar keep the view quantitative either way.</p>"),
             m_siteView, 0);
        step(tr("Coordinates"),
             tr("<p>Point x and y are <b>easting and northing</b> in the project "
                "coordinate system, here ETRS89 / UTM zone 32N. Change it under "
                "<i>Project &gt; Coordinate system</i>.</p>"
                "<p>The z coordinate means the same as in the laboratory: height "
                "above the bed, in metres.</p>"),
             m_siteView, 0);
    } else {
        step(tr("The flume"),
             tr("<p>This is a top view of the flume, with the inlet on the left and "
                "the flow running to the right. Each circle is a <b>measurement "
                "location</b>.</p>"
                "<p>Click anywhere to place a new point, or click an existing one "
                "to edit its height, water depth, time window and filters.</p>"),
             m_siteView, 0);
        step(tr("Points and verticals"),
             tr("<p>Several points sharing an x-y location form a <b>vertical "
                "profile</b>. The example holds five heights at one location plus a "
                "single point further downstream.</p>"
                "<p>Setting the water depth at one point applies it to every point "
                "of that vertical.</p>"),
             m_siteView, 0);
    }

    step(tr("Time series"),
         tr("<p>Pick a point and a data series, then press <i>Add</i> to plot it. "
            "Series from different points can be superimposed, and <i>Style</i> "
            "changes line and marker appearance.</p>"
            "<p>Besides the measured columns there is a derived series, %1, "
            "computed as 0.5 (var U + var V + var W).</p>")
             .arg(field ? tr("<b>TKE proxy</b>") : tr("<b>TKE</b>")),
         plotArea, 0);

    if (field) {
        step(tr("Why \"proxy\""),
             tr("<p>A FlowTracker2 point is roughly 60 samples at 2 Hz over 30 s. "
                "That resolves nothing above 1 Hz, so the variance it yields is not "
                "the turbulent kinetic energy a laboratory record measures.</p>"
                "<p>It stays a useful <i>relative</i> indicator between stations of "
                "one survey, which is why it is labelled as a proxy everywhere "
                "instead of being hidden or silently renamed. Dissipation is "
                "reported as not estimable for the same reason.</p>"),
             plotArea, 0);
    }

    step(tr("Despiking"),
         tr("<p>Open a point from the %1 and use the filter section: correlation "
            "and SNR thresholds, a velocity threshold, and the Goring &amp; Nikora "
            "method. Removed samples are either left as gaps or interpolated.</p>"
            "<p>%2</p>")
             .arg(field ? tr("map") : tr("flume"),
                  field ? tr("Field imports start with the correlation filter "
                             "<b>off</b>: the instrument reports correlation on a "
                             "different scale, where the laboratory default of 70 "
                             "would reject nearly every sample.")
                        : tr("The example enables the velocity threshold, which "
                             "removes samples further than three standard "
                             "deviations from the mean.")),
         m_siteView, 0);

    step(tr("Vertical profiles"),
         tr("<p>The second tab plots the profile of a vertical against z or z/h, "
            "with the statistics of each height beside it: means, standard "
            "deviations, skewness and kurtosis, the Reynolds stresses and %1.</p>"
            "<p>This is also where <b>probe alignment</b> is corrected: heading, "
            "pitch and roll can be proposed automatically so the mean V and W of "
            "the profile go to zero, or set by hand.</p>")
             .arg(field ? tr("the TKE proxy") : tr("TKE and dissipation")),
         m_profileFrame, 1);

    step(tr("Exporting"),
         tr("<p>Under <i>Export</i> you can write the shown series as CSV, the "
            "current frame as a 300 dpi PNG%1, and per-point or per-profile "
            "statistics as xlsx, including into the bundled profile template that "
            "adds velocity magnitude and direction.</p>"
            "<p>Under <i>File &gt; Save project as</i> the whole analysis, raw data "
            "included, goes into a single .advProj file that opens on any "
            "computer.</p>")
             .arg(field ? tr(", the map with its OpenStreetMap attribution")
                        : QString()),
         menuBar(), -1);

    step(tr("That is the tour"),
         tr("<p>You can reopen it any time from <i>Help &gt; Restart guided "
            "tour</i>, and load the other example from the same menu.</p>"
            "<p>The full documentation, including the file formats and the "
            "definitions behind every statistic, is at "
            "<a href=\"https://adv-explorer.readthedocs.io/\">"
            "adv-explorer.readthedocs.io</a>.</p>"),
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
    QCoreApplication::processEvents();
    ok = snap(this, QStringLiteral("guided-tour.png")) && ok;
    if (m_tour)
        m_tour->close();

    return ok;
}
