// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * aimodelstable.cpp: Widget for models table in settings
 *****************************************************************************
 * Copyright © 2026 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>

#include "aimodelstable.hpp"

namespace {

// sorts the Size column by raw byte count (stored in Qt::UserRole) instead of
// lexicographically on the formatted "12 MB" text.
class SizeItem : public QTableWidgetItem
{
    public:
        using QTableWidgetItem::QTableWidgetItem;
        bool operator<(const QTableWidgetItem &other) const override
        {
            return data(Qt::UserRole).toLongLong()
                 < other.data(Qt::UserRole).toLongLong();
        }
};

// on-disk size of a model entry: a single file (e.g. a .bin) or the recursive
// content of a directory (e.g. a .mlmodelc bundle).
qint64 entrySize(const QFileInfo &info)
{
    if (!info.isDir())
        return info.size();
    qint64 total = 0;
    const QFileInfoList children = QDir(info.absoluteFilePath())
            .entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &child : children)
        total += entrySize(child);
    return total;
}

QString formatSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    if (bytes >= 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 1);
    if (bytes >= 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 B").arg(bytes);
}

// fixed width of the trailing remove-button column.
const int REMOVE_COL_WIDTH = 28;

} // namespace

AiModelsTable::AiModelsTable(QWidget *parent)
    : QTableWidget(0, 3, parent)
{
    this->setHorizontalHeaderLabels({qtr("Name"), qtr("Size"), QString()});
    QHeaderView *header = this->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    header->resizeSection(2, REMOVE_COL_WIDTH);
    this->verticalHeader()->hide();
}

void
AiModelsTable::init(const QDir &models_dir)
{
    // discard previous rows and pending removals so this also serves as reload.
    this->setSortingEnabled(false);
    this->clearContents();
    this->clearSpans();
    this->setRowCount(0);
    m_aiModelsToDelete.clear();

    if (models_dir.exists())
    {
        const QFileInfoList entries = models_dir.entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (QFileInfo entry : entries)
            addRow(entry);
    }

    // let the user sort by Name or Size by clicking the headers.
    this->setSortingEnabled(true);
    this->sortByColumn(0, Qt::AscendingOrder);
    refreshPlaceholder();
}

void
AiModelsTable::commit()
{
    // deletions are deferred to here (Save); cancelling never reaches commit.
    for (const QString &path : m_aiModelsToDelete)
    {
        QFileInfo fi(path);
        if (fi.isDir())
            QDir(path).removeRecursively();
        else
            QFile::remove(path);
    }
    m_aiModelsToDelete.clear();
}

void
AiModelsTable::addRow(QFileInfo &file_info)
{
    const qint64 bytes = entrySize(file_info);
    const int row = this->rowCount();
    this->insertRow(row);

    auto *nameItem = new QTableWidgetItem(file_info.fileName());
    nameItem->setData(Qt::UserRole, file_info.absoluteFilePath());
    this->setItem(row, 0, nameItem);

    auto *sizeItem = new SizeItem(formatSize(bytes));
    sizeItem->setData(Qt::UserRole, (qlonglong)bytes);
    this->setItem(row, 1, sizeItem);

    // a small minus button to stage this model for removal.
    auto *removeButton = new QPushButton("-", this);
    removeButton->setFixedSize(18, 18);
    removeButton->setAccessibleName(qtr("Remove"));
    removeButton->setToolTip(qtr("Remove this model"));

    auto *removeContainer = new QWidget(this);
    auto *removeLayout = new QHBoxLayout(removeContainer);
    removeLayout->addWidget(removeButton);
    removeLayout->setAlignment(Qt::AlignCenter);
    removeLayout->setContentsMargins(0, 0, 0, 0);
    this->setCellWidget(row, 2, removeContainer);

    this->connect(removeButton, &QPushButton::clicked, this, [this, nameItem]() {
        const QString path = nameItem->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            m_aiModelsToDelete.append(path);
        // the row index shifts as rows are sorted or removed, so resolve it
        // from the item itself.
        this->removeRow(this->row(nameItem));
        refreshPlaceholder();
    });
}

void
AiModelsTable::refreshPlaceholder()
{
    if (this->rowCount() != 0)
        return;

    this->insertRow(0);
    auto *placeholder = new QTableWidgetItem(qtr("No models found"));
    placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsEnabled);
    this->setItem(0, 0, placeholder);
    this->setSpan(0, 0, 1, this->columnCount());
}
