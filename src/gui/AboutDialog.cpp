/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About ADV-Explorer"));

    auto *layout = new QVBoxLayout(this);
    auto *logo = new QLabel(this);
    logo->setPixmap(QPixmap(QStringLiteral(":/img/logo.png"))
                        .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignHCenter);
    layout->addWidget(logo);

    auto *text = new QLabel(this);
    text->setTextFormat(Qt::RichText);
    text->setOpenExternalLinks(true);
    text->setWordWrap(true);
    text->setText(tr(
        "<h2>ADV-Explorer %1</h2>"
        "<p>Interactive analysis of acoustic Doppler velocimetry (ADV) "
        "measurements: time series, despiking, turbulence statistics and "
        "vertical profiles.</p>"
        "<p>Author: Sebastian Schwindt (2026)</p>"
        "<p>Source code: <a href=\"%2\">%2</a></p>"
        "<p>License: <a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">"
        "GNU General Public License v3</a>. This program comes without any "
        "warranty; see the LICENSE file for details. Plotting is powered by "
        "QCustomPlot (GPLv3); Excel output by QXlsx (MIT); spectra by "
        "KissFFT (BSD).</p>")
        .arg(QStringLiteral(ADV_EXPLORER_VERSION), QStringLiteral(ADV_EXPLORER_URL)));
    layout->addWidget(text);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}
