/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include "core/ProjectSettings.h"

#include <QMainWindow>
#include <QUuid>

class GuidedTour;
class PlotFrame;
class ProfileFrame;
class SiteView;
class QAction;
class QSplitter;
class QTabWidget;
class QVBoxLayout;

namespace adv {
class ProjectModel;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openProject(const QString &filePath);

    /// Build a small demo project from repo data and save the documentation
    /// screenshots (invoked by the --screenshots command line mode).
    bool captureDocScreenshots(const QString &outputDir);

private slots:
    void newProject();
    void openProjectDialog();
    bool saveProject();
    bool saveProjectAs();
    void importFiles();
    void importFlowTrackerSurvey();
    void chooseProjectCrs();
    void exportCsv();
    void exportPng();
    void exportMapPng();
    void exportPointStats();
    void exportProfileStats();
    void addSecondPlotFrame();
    void removeSecondPlotFrame();
    void createPointAt(double x, double y);
    void editPoint(const QUuid &pointId);
    void showAbout();
    void openDocumentation();
    void loadLabExample();
    void loadFieldExample();
    void startGuidedTour();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildMenus();
    /// Give the site view 2/5 of the splitter height. Must run once the splitter
    /// actually has a height; see showEvent().
    void applySplitterProportions();
    /// Is the site view actually in the layout, visible, and given room? The
    /// screenshot mode refuses to run when it is not; see the definition.
    bool siteViewIsUsable() const;
    /// Replace the project with one of the built-in examples and offer the tour.
    void loadExample(adv::Mode mode);
    /// Ask before an action throws away measurement points the user may want.
    bool confirmReplaceProject();
    /// Rebuild the tour steps for the campaign mode currently shown.
    void configureTour();
    /// Put the site view matching the campaign mode into the splitter.
    void applyMode(adv::Mode mode);
    void collectPlotSettings();
    void applyPlotSettings();
    bool writeProject(const QString &filePath);

    adv::ProjectModel *m_model;
    SiteView *m_siteView = nullptr;
    QSplitter *m_splitter = nullptr;
    QTabWidget *m_tabs;
    QVBoxLayout *m_plotColumn;
    QList<PlotFrame *> m_plotFrames;
    ProfileFrame *m_profileFrame;
    QString m_projectPath;
    QAction *m_removeFrameAction = nullptr;
    QAction *m_labModeAction = nullptr;
    QAction *m_fieldModeAction = nullptr;
    QAction *m_crsAction = nullptr;
    QAction *m_w2Action = nullptr;
    QAction *m_importFtAction = nullptr;
    QAction *m_exportMapAction = nullptr;
    GuidedTour *m_tour = nullptr;
    bool m_splitProportioned = false; ///< the one-time split on first show
};
