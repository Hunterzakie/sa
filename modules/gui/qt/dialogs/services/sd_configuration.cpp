/*****************************************************************************
 * sd_configuration.cpp: Generic configurable services discovery dialog
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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "sd_configuration.hpp"

#include <vlc_media_source.h>

#include <climits>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QUrl>

SDConfigDialog::SDConfigDialog( qt_intf_t *_p_intf, const QString& name,
                                const QString& title )
    : QVLCDialog( nullptr, _p_intf )
    , m_name( name )
{
    setWindowTitle( title.isEmpty() ? qtr( "Configure" ) : title );
    buildForm();
}

SDConfigDialog::~SDConfigDialog()
{
    releaseSource();
}

void SDConfigDialog::buildForm()
{
    /* Query the entry schema so we can generate the matching editor. This opens
     * the SD; we keep the reference for the (imminent) first show. */
    size_t schemaCount = 0;
    const vlc_sd_field_desc_t *schema = ensureSource()
        ? vlc_media_source_ConfigDescribe( m_source, &schemaCount ) : nullptr;

    m_list = new QListWidget( this );
    m_list->setAlternatingRowColors( true );

    QGridLayout *layout = new QGridLayout( this );
    int row = 0;
    layout->addWidget( m_list, row++, 0, 1, 3 );

    for( size_t i = 0; i < schemaCount; i++ )
    {
        const vlc_sd_field_desc_t *f = &schema[i];
        const QString cleanLabel = f->label ? qtr( f->label ) : qfu( f->key );
        const QString label = f->required ? cleanLabel + QStringLiteral( " *" )
                                          : cleanLabel;
        QWidget *editor;
        bool withBrowse = false;
        QHash<int, QString> choiceLabels; /* choice value -> label, CHOICE only */

        switch( f->type )
        {
        case SD_FIELD_SECRET:
        {
            QLineEdit *edit = new QLineEdit( this );
            edit->setEchoMode( QLineEdit::Password );
            connect( edit, &QLineEdit::returnPressed, this, &SDConfigDialog::add );
            editor = edit;
            break;
        }
        case SD_FIELD_CHOICE:
        {
            QComboBox *combo = new QComboBox( this );
            for( size_t j = 0; j < f->choice_count; j++ )
            {
                const vlc_sd_field_choice_t *c = &f->choices[j];
                /* show the label, store the machine value (int) as item data */
                const QString text = qtr( c->label );
                combo->addItem( text, c->value );
                choiceLabels.insert( c->value, text );
            }
            editor = combo;
            break;
        }
        case SD_FIELD_INTEGER:
        {
            QSpinBox *spin = new QSpinBox( this );
            spin->setRange( INT_MIN, INT_MAX );
            editor = spin;
            break;
        }
        case SD_FIELD_BOOLEAN:
            editor = new QCheckBox( this );
            break;
        case SD_FIELD_FILE:
        case SD_FIELD_TEXT:
        default:
        {
            QLineEdit *edit = new QLineEdit( this );
            connect( edit, &QLineEdit::returnPressed, this, &SDConfigDialog::add );
            editor = edit;
            withBrowse = ( f->type == SD_FIELD_FILE );
            break;
        }
        }

        layout->addWidget( new QLabel( label, this ), row, 0 );
        layout->addWidget( editor, row, 1 );
        if( withBrowse )
        {
            QPushButton *browse = new QPushButton( qtr( "Browse..." ), this );
            browse->setToolTip( qtr( "Adds local files" ) );
            layout->addWidget( browse, row, 2 );
            connect( browse, &QPushButton::clicked, this, &SDConfigDialog::addFiles );
        }

        m_fields.push_back( { qfu( f->key ), cleanLabel, (int)f->type,
                              f->required, editor, choiceLabels } );
        row++;
    }

    QPushButton *addButton = new QPushButton( qtr( "Add" ), this );
    QPushButton *deleteButton = new QPushButton( qtr( "Delete" ), this );
    deleteButton->setToolTip( qtr( "Deletes the selected item" ) );

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addStretch();
    actions->addWidget( addButton );
    actions->addWidget( deleteButton );
    layout->addLayout( actions, row++, 0, 1, 3 );

    QDialogButtonBox *buttonBox = new QDialogButtonBox( this );
    QPushButton *closeButton = new QPushButton( qtr( "&Close" ), this );
    buttonBox->addButton( closeButton, QDialogButtonBox::AcceptRole );
    layout->addWidget( buttonBox, row++, 0, 1, 3 );

    resize( 500, 350 );

    connect( addButton, &QPushButton::clicked, this, &SDConfigDialog::add );
    connect( deleteButton, &QPushButton::clicked, this, &SDConfigDialog::remove );
    connect( closeButton, &QPushButton::clicked, this, &SDConfigDialog::close );
}

bool SDConfigDialog::ensureSource()
{
    if( m_source )
        return true;

    libvlc_int_t *libvlc = vlc_object_instance( p_intf );
    vlc_media_source_provider_t *provider =
        vlc_media_source_provider_Get( libvlc );
    if( provider )
        m_source = vlc_media_source_provider_GetMediaSource( provider,
                                                             qtu( m_name ) );
    return m_source != nullptr;
}

void SDConfigDialog::releaseSource()
{
    if( m_source )
    {
        vlc_media_source_Release( m_source );
        m_source = nullptr;
    }
}

void SDConfigDialog::showEvent( QShowEvent *event )
{
    QVLCDialog::showEvent( event );
    /* Hold the SD open only while the dialog is visible. */
    ensureSource();
    reload();
}

void SDConfigDialog::hideEvent( QHideEvent *event )
{
    QVLCDialog::hideEvent( event );
    releaseSource();
}

void SDConfigDialog::reload()
{
    m_list->clear();
    if( !m_source )
        return;

    size_t count = 0;
    vlc_sd_input_t *entries = vlc_media_source_ConfigList( m_source, &count );

    for( size_t i = 0; i < count; i++ )
    {
        const vlc_sd_input_t *e = &entries[i];
        const int id = e->id;

        /* Build a readable label from the (non-secret) field values. */
        QStringList parts;
        for( size_t j = 0; j < e->field_count; j++ )
        {
            const vlc_sd_field_t *fv = &e->fields[j];
            const QString fieldKey = qfu( fv->key );
            const Field *field = nullptr;
            for( const Field& f : m_fields )
                if( f.key == fieldKey )
                {
                    field = &f;
                    break;
                }

            QString text;
            switch( fv->type )
            {
            case SD_FIELD_SECRET:
                continue;   /* never shown (and stripped from LIST anyway) */
            case SD_FIELD_INTEGER:
                text = QString::number( (qlonglong)fv->integer );
                break;
            case SD_FIELD_CHOICE:
            {
                const int val = (int)fv->integer;
                text = field ? field->choiceLabels.value( val,
                                   QString::number( val ) )
                             : QString::number( val );
                break;
            }
            case SD_FIELD_BOOLEAN:
                if( !fv->boolean )
                    continue;   /* only surface enabled booleans */
                text = field ? field->label : fieldKey;
                break;
            default: /* string types */
                if( !fv->value || fv->value[0] == '\0' )
                    continue;
                text = qfu( fv->value );
                break;
            }
            parts << text;
        }

        const QString display = parts.isEmpty()
            ? qtr( "Entry %1" ).arg( id )
            : parts.join( QStringLiteral( " · " ) );

        QListWidgetItem *item = new QListWidgetItem( display, m_list );
        /* Keep the opaque id so removal targets exactly this entry. */
        item->setData( Qt::UserRole, id );
    }
    vlc_sd_input_ListFree( entries, count );
}

void SDConfigDialog::add()
{
    if( !ensureSource() )
        return;

    /* Backing storage for the borrowed C strings; must outlive the call. */
    std::vector<QByteArray> bufs;
    bufs.reserve( m_fields.size() * 2 );
    std::vector<vlc_sd_field_t> values;
    values.reserve( m_fields.size() );

    bool any = false;
    for( const Field& field : m_fields )
    {
        vlc_sd_field_t v = {};
        bufs.push_back( field.key.toUtf8() );
        v.key = bufs.back().data();
        v.type = (vlc_sd_field_type_e)field.type;

        switch( field.type )
        {
        case SD_FIELD_INTEGER:
            if( QSpinBox *s = qobject_cast<QSpinBox *>( field.widget ) )
                v.integer = s->value();
            break;
        case SD_FIELD_BOOLEAN:
            if( QCheckBox *c = qobject_cast<QCheckBox *>( field.widget ) )
                v.boolean = c->isChecked();
            break;
        case SD_FIELD_CHOICE:
            if( QComboBox *c = qobject_cast<QComboBox *>( field.widget ) )
                v.integer = c->currentData().toInt();
            break;
        default: /* string types */
        {
            QLineEdit *e = qobject_cast<QLineEdit *>( field.widget );
            const QString s = e ? ( field.type == SD_FIELD_SECRET
                                        ? e->text() : e->text().trimmed() )
                                : QString();
            if( field.required && s.isEmpty() )
            {
                /* a required field is missing: point the user at it */
                field.widget->setFocus();
                return;
            }
            if( s.isEmpty() )
                continue;   /* omit empty optional string field */
            any = true;
            bufs.push_back( s.toUtf8() );
            v.value = bufs.back().data();
            break;
        }
        }
        values.push_back( v );
    }

    /* an entry needs at least one non-empty text field */
    if( !any )
        return;

    vlc_sd_input_t input = {};
    input.fields = values.data();
    input.field_count = values.size();
    /* on success the new entry's id is returned, which is strictly positive */
    if( vlc_media_source_ConfigAdd( m_source, &input ) > 0 )
    {
        clearInputs();
        reload();
    }
}

void SDConfigDialog::addFiles()
{
    /* Browsing fills the file field of the form; the entry is only submitted
     * when the user presses Add. Submitting the file on its own would drop
     * every other field, and be rejected outright by any SD whose required
     * field is not the file one. */
    QLineEdit *edit = nullptr;
    for( const Field& field : m_fields )
        if( field.type == SD_FIELD_FILE )
        {
            edit = qobject_cast<QLineEdit *>( field.widget );
            break;
        }
    if( edit == nullptr )
        return;

    const QString file =
        QFileDialog::getOpenFileName( this, qtr( "Select a file" ), QString() );
    if( !file.isEmpty() )
        edit->setText( QUrl::fromLocalFile( file ).toString() );
}

void SDConfigDialog::remove()
{
    QListWidgetItem *item = m_list->currentItem();
    if( !item || !ensureSource() )
        return;

    vlc_media_source_ConfigRemove( m_source, item->data( Qt::UserRole ).toInt() );
    reload();
}

void SDConfigDialog::clearInputs()
{
    for( const Field& field : m_fields )
    {
        if( QLineEdit *edit = qobject_cast<QLineEdit *>( field.widget ) )
            edit->clear();
        else if( QComboBox *combo = qobject_cast<QComboBox *>( field.widget ) )
            combo->setCurrentIndex( 0 );
        else if( QSpinBox *spin = qobject_cast<QSpinBox *>( field.widget ) )
            spin->setValue( 0 );
        else if( QCheckBox *check = qobject_cast<QCheckBox *>( field.widget ) )
            check->setChecked( false );
    }
}
