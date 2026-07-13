/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#include "gui/MainWindow.h"

#include <QApplication>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ADV-Explorer"));
    app.setApplicationVersion(QStringLiteral(ADV_EXPLORER_VERSION));
    app.setOrganizationName(QStringLiteral("sschwindt"));
    app.setWindowIcon(QIcon(QStringLiteral(":/img/logo.png")));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    MainWindow window;
    window.show();

    // open a project passed on the command line
    const QStringList args = app.arguments();
    if (args.size() > 1 && args.at(1).endsWith(QStringLiteral(".advProj")))
        window.openProject(args.at(1));

    return app.exec();
}
