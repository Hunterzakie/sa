// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * aimodelstable.hpp: Widget for models table in settings
 *****************************************************************************
 * Copyright © 2026 Videolabs, VideoLAN and VLC authors
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifndef VLC_QT_AIMODELSTABLE_HPP
#define VLC_QT_AIMODELSTABLE_HPP

#include "qt.hpp"

#include <QDir>
#include <QStringList>
#include <QTableWidget>

class AiModelsTable : public QTableWidget
{
    Q_OBJECT

    public:
        AiModelsTable(QWidget *parent = nullptr);

        // load the the table with files in the directory
        void init(const QDir &models_dir);

        // call 'commit' to apply changes
        void commit();

    private:
        void addRow(QFileInfo &file_info);
        // show a placeholder row when the table is empty
        void refreshPlaceholder();

        QStringList m_aiModelsToDelete;

};

#endif /* VLC_QT_AIMODELSTABLE_HPP */
