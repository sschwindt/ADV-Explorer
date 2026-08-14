/*
 * ADV-Explorer - interactive analysis of ADV measurements
 * Copyright (C) 2026 Sebastian Schwindt
 * Licensed under the GNU General Public License v3 or later.
 */
#include "CrsDialog.h"

#include "core/Crs.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

using namespace adv;

CrsDialog::CrsDialog(int currentEpsg, QWidget *parent)
    : QDialog(parent)
    , m_epsg(currentEpsg)
{
    setWindowTitle(tr("Project coordinate system"));
    setMinimumSize(520, 460);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        tr("Measurement positions are stored as easting and northing in this system.\n"
           "The z coordinate stays a height above the river bed, as in the flume."),
        this));

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter by EPSG code or name, e.g. 25832 or \"UTM zone 32\""));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &CrsDialog::applyFilter);
    layout->addWidget(m_filterEdit);

    m_list = new QListWidget(this);
    for (const int epsg : crs::supportedCodes()) {
        auto *item = new QListWidgetItem(
            QStringLiteral("EPSG:%1  %2").arg(epsg).arg(crs::name(epsg)), m_list);
        item->setData(Qt::UserRole, epsg);
    }
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            m_epsg = item->data(Qt::UserRole).toInt();
        updateDetails();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    layout->addWidget(m_list, 1);

    m_details = new QLabel(this);
    m_details->setWordWrap(true);
    m_details->setMinimumHeight(56);
    layout->addWidget(m_details);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    selectEpsg(currentEpsg != 0 ? currentEpsg : 25832);
}

void CrsDialog::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        item->setHidden(!needle.isEmpty()
                        && !item->text().contains(needle, Qt::CaseInsensitive));
    }

    // typing a bare code jumps straight to it, which is how most users arrive
    bool isNumber = false;
    const int typed = needle.toInt(&isNumber);
    if (isNumber && crs::isSupported(typed))
        selectEpsg(typed);
}

void CrsDialog::selectEpsg(int epsg)
{
    for (int row = 0; row < m_list->count(); ++row) {
        QListWidgetItem *item = m_list->item(row);
        if (item->data(Qt::UserRole).toInt() != epsg)
            continue;
        m_list->setCurrentItem(item);
        m_list->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        m_epsg = epsg;
        break;
    }
    updateDetails();
}

void CrsDialog::updateDetails()
{
    if (!crs::isSupported(m_epsg)) {
        m_details->setText(crs::supportedRangesText());
        return;
    }

    QString text = QStringLiteral("EPSG:%1 - %2").arg(m_epsg).arg(crs::name(m_epsg));
    if (crs::isApproximate(m_epsg)) {
        text += QStringLiteral(
            "\n\nNote: this system is referenced to WGS 84 through a single countrywide "
            "Helmert transformation, so positions on the basemap can be off by about a "
            "metre. Distances and angles between measurement points are unaffected.");
    }
    m_details->setText(text);
}
