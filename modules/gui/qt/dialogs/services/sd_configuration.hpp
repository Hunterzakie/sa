/*****************************************************************************
 * sd_configuration.hpp: Generic configurable services discovery dialog
 *****************************************************************************
 * Copyright (C) 2026 the VideoLAN team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef QVLC_SD_CONFIGURATION_DIALOG_H_
#define QVLC_SD_CONFIGURATION_DIALOG_H_ 1

#include "widgets/native/qvlcframe.hpp"

#include <QHash>
#include <QVector>

struct vlc_media_source_t;

class QWidget;
class QListWidget;
class QShowEvent;
class QHideEvent;

/**
 * Generic editor for a configurable services discovery.
 *
 * The whole UI is generated from the SD's SD_CMD_CONFIG_DESC descriptor: one
 * editor widget is created per field the SD declares (a text field, a password
 * field, a combo box or a file picker depending on the field type), so any SD
 * advertising SD_CAP_CONFIGURABLE gets a suitable editor without any
 * SD-specific code. Entries are listed, added and removed live through the
 * SD_CMD_CONFIG_* control commands.
 */
class SDConfigDialog : public QVLCDialog
{
    Q_OBJECT

public:
    SDConfigDialog( qt_intf_t *, const QString& name, const QString& title );
    ~SDConfigDialog() override;

protected:
    void showEvent( QShowEvent * ) override;
    void hideEvent( QHideEvent * ) override;

private slots:
    void add();
    void addFiles();
    void remove();

private:
    /* One editor generated from a declared field. type is an vlc_sd_field_type_e. */
    struct Field
    {
        QString key;
        QString label;
        int type;
        bool required;
        QWidget *widget;
        QHash<int, QString> choiceLabels; /* choice value -> label, CHOICE only */
    };

    bool ensureSource();
    void releaseSource();
    void buildForm();
    void reload();
    void clearInputs();

    QString m_name;
    struct vlc_media_source_t *m_source = nullptr;

    QVector<Field> m_fields;
    QListWidget *m_list = nullptr;
};

#endif
