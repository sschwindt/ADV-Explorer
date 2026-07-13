/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#pragma once

#include <QMainWindow>
#include <QUuid>

class FlumeView;
class PlotFrame;
class ProfileFrame;
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

private slots:
    void newProject();
    void openProjectDialog();
    bool saveProject();
    bool saveProjectAs();
    void importFiles();
    void exportCsv();
    void exportPng();
    void exportPointStats();
    void exportProfileStats();
    void addSecondPlotFrame();
    void removeSecondPlotFrame();
    void createPointAt(double x, double y);
    void editPoint(const QUuid &pointId);
    void showAbout();

private:
    void buildMenus();
    void collectPlotSettings();
    void applyPlotSettings();
    bool writeProject(const QString &filePath);

    adv::ProjectModel *m_model;
    FlumeView *m_flumeView;
    QTabWidget *m_tabs;
    QVBoxLayout *m_plotColumn;
    QList<PlotFrame *> m_plotFrames;
    ProfileFrame *m_profileFrame;
    QString m_projectPath;
    QAction *m_removeFrameAction = nullptr;
};
