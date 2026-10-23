/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
 * Copyright (C) 2026 Vinit Rajpurohit (spidey889)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

// Keep the association lifecycle separate from first-run configuration/media
// library handling. Both actions still use FirstRunWizard::finish().
#include "qt.hpp"
#include "firstrunwizard.hpp"

#include <QPointer>
#include <QShowEvent>

#ifdef _WIN32
# include "util/win32_association_task.hpp"
# include "util/win32_association_types.hpp"
# include <QMessageBox>
# include <QPalette>
# include <QThreadPool>
#endif

FirstRunWizard::~FirstRunWizard()
{
#ifdef _WIN32
    // Abandon delivery, not a batch already writing: the backend owns recovery.
    if( m_associationTask != nullptr )
        m_associationTask->abandon();
#endif
}

void FirstRunWizard::showEvent( QShowEvent *event )
{
#ifdef _WIN32
    // DialogsProvider retains this wizard. An explicit show is a fresh
    // interaction; a window-manager restore must not reset completion state.
    if( !event->spontaneous() && m_associationTask == nullptr )
        m_finished = false;
#endif
    QWizard::showEvent( event );
}

void FirstRunWizard::done( int result )
{
#ifdef _WIN32
    if( m_associationTask != nullptr || m_finished )
        return;
    m_finished = true;
#endif
    QWizard::done( result );
}

void FirstRunWizard::complete()
{
#ifdef _WIN32
    if( m_associationTask != nullptr )
        return;
    // Prevent re-entry through configuration or accepted/finished signals.
    m_finished = true;
#endif
    const QPointer<FirstRunWizard> guard( this );
    finish();
    if( guard )
        QWizard::done( QDialog::Accepted );
    // done() may synchronously destroy this wizard. Do not access it afterward.
}

void FirstRunWizard::accept()
{
#ifdef _WIN32
    if( m_associationTask != nullptr || m_finished )
        return;
    if( currentId() == DEFAULT_APPS_PAGE )
    {
        applyDefaultApps();
        return;
    }
#endif
    complete();
}

#ifdef _WIN32
void FirstRunWizard::setupDefaultAppsPage()
{
    setOption( QWizard::HaveCustomButton1 );
    setButtonText( QWizard::CustomButton1, qtr( "Not now" ) );
    setButtonText( QWizard::FinishButton, qtr( "Set as default" ) );
    setButtonLayout( { QWizard::Stretch, QWizard::BackButton,
                       QWizard::CustomButton1, QWizard::NextButton,
                       QWizard::FinishButton } );

    connect( this, &QWizard::customButtonClicked, this, [this]( int button ) {
        if( button == QWizard::CustomButton1 )
            skipDefaultApps();
    } );
    connect( this, &QWizard::currentIdChanged,
             this, &FirstRunWizard::updateDefaultAppsPage );
    updateDefaultAppsPage( currentId() );
}

static QString defaultAppsPrimaryButtonStyle( const QPalette& palette )
{
    const QColor accent = palette.color( QPalette::Highlight );
    const QColor foreground = palette.color( QPalette::HighlightedText );
    const QColor hover = accent.lighter( 112 );
    const QColor pressed = accent.darker( 112 );
    const QColor disabledBackground = palette.color( QPalette::Disabled,
                                                      QPalette::Button );
    const QColor disabledForeground = palette.color( QPalette::Disabled,
                                                      QPalette::ButtonText );

    return QStringLiteral(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %1; "
        "border-radius: 4px; padding: 4px 14px; }"
        "QPushButton:hover { background-color: %3; border-color: %3; }"
        "QPushButton:pressed { background-color: %4; border-color: %4; }"
        "QPushButton:focus { outline: none; border-color: %2; }"
        "QPushButton:disabled { background-color: %5; color: %6; border-color: %5; }" )
        .arg( accent.name(), foreground.name(), hover.name(), pressed.name(),
              disabledBackground.name(), disabledForeground.name() );
}

void FirstRunWizard::updateDefaultAppsPage( int id )
{
    const bool isDefaultAppsPage = id == DEFAULT_APPS_PAGE;
    QAbstractButton *backButton = button( QWizard::BackButton );
    QAbstractButton *notNowButton = button( QWizard::CustomButton1 );
    QAbstractButton *finishButton = button( QWizard::FinishButton );

    notNowButton->setVisible( isDefaultAppsPage );
    if( isDefaultAppsPage )
    {
        backButton->setMinimumSize( 88, 28 );
        notNowButton->setMinimumSize( 88, 28 );
        finishButton->setMinimumSize( 112, 28 );
        finishButton->setStyleSheet( defaultAppsPrimaryButtonStyle( palette() ) );
    }
    else
    {
        backButton->setMinimumSize( 0, 0 );
        notNowButton->setMinimumSize( 0, 0 );
        finishButton->setMinimumSize( 0, 0 );
        finishButton->setStyleSheet( {} );
    }
}

void FirstRunWizard::skipDefaultApps()
{
    if( currentId() != DEFAULT_APPS_PAGE || m_associationTask != nullptr || m_finished )
        return;
    complete();
}

void FirstRunWizard::applyDefaultApps()
{
    if( m_associationTask != nullptr || m_finished )
        return;

    auto *task = new vlc::win32::AssociationTask(
        vlc::win32::allMediaExtensions() );
    m_associationTask = task;
    button( QWizard::BackButton )->setEnabled( false );
    button( QWizard::CustomButton1 )->setEnabled( false );
    button( QWizard::NextButton )->setEnabled( false );
    button( QWizard::FinishButton )->setEnabled( false );
    setCursor( Qt::WaitCursor );

    connect( task, &BaseAsyncTask::result, this, [this, task]() {
        const vlc::win32::AssociationBatchResult batch = task->takeResult();
        task->abandon();
        m_associationTask = nullptr;
        m_finished = true;
        unsetCursor();
        button( QWizard::BackButton )->setEnabled( true );
        button( QWizard::CustomButton1 )->setEnabled( true );
        button( QWizard::NextButton )->setEnabled( true );
        button( QWizard::FinishButton )->setEnabled( true );

        // DialogsProvider::quit marks the application before teardown. Do not
        // create new windows while VLC is shutting down.
        if( qApp->property( "isDying" ).toBool() )
            return;

        msg_Dbg( p_intf, "automatic default-app setup: %u changed, %u unchanged, %zu need confirmation, %zu failed",
                 batch.changed, batch.unchanged, batch.needsUserChoice.size(), batch.failures.size() );
        for( const auto &[extension, error] : batch.needsUserChoice )
            msg_Warn( p_intf, "automatic default-app setup for %ls needs confirmation: %s",
                      extension.c_str(), error.c_str() );
        for( const auto &[extension, error] : batch.failures )
            msg_Warn( p_intf, "automatic default-app setup for %ls failed: %s",
                      extension.c_str(), error.c_str() );

        if( !batch.failures.empty() || !batch.needsUserChoice.empty() )
        {
            QString message = batch.changed == 0 && batch.unchanged == 0
                ? qtr( "VLC could not be made your default media player." )
                : qtr( "VLC could not be made the default for some file types." );
            if( !batch.needsUserChoice.empty() )
            {
                message += QStringLiteral( "\n" );
                message += qtr( "You can change defaults manually in Windows Settings > Apps > Default apps." );
            }

            // Success stays quiet. Failure notices outlive the hidden wizard,
            // but are still owned by its parent when one exists.
            auto *notice = new QMessageBox(
                QMessageBox::Warning, qtr( "Default media player" ), message,
                QMessageBox::Ok, parentWidget() );
            notice->setTextFormat( Qt::PlainText );
            notice->setAttribute( Qt::WA_DeleteOnClose );
            // Show after first run closes, without a nested event loop or an
            // access to this/p_intf after accepted/finished may destroy them.
            QMetaObject::invokeMethod( notice, [notice] {
                if( qApp->property( "isDying" ).toBool() )
                    notice->deleteLater();
                else
                    notice->open();
            },
                                       Qt::QueuedConnection );
        }

        complete();
    } );
    task->start( *QThreadPool::globalInstance() );
}
#endif
