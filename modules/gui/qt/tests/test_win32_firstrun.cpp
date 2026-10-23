/*****************************************************************************
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


#include "qt.hpp"
#include "../dialogs/firstrun/firstrunwizard.hpp"
#include "../util/win32_associations.hpp"
#include "../util/win32_association_task.hpp"
#include "../util/win32_association_types.hpp"

#include <windows.h>
#include <shellapi.h>
#include <QApplication>
#include <QMessageBox>
#include <QPointer>
#include <QSemaphore>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QThread>
#include <QtPlugin>

#include <atomic>
#include <cstdarg>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace
{
std::mutex backendMutex;
std::vector<std::wstring> writes;
std::map<std::wstring, vlc::win32::AssociationResult> results;
std::atomic_bool pauseWrite{false}, offUiThread{true};
QSemaphore entered, resume;
unsigned finishes = 0, settingsLaunches = 0;
std::function<void()> finishHook;
QStringList warnings;

void testWarn(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    warnings.append(QString::vasprintf(format, arguments));
    va_end(arguments);
}

[[maybe_unused]] HINSTANCE WINAPI testLaunchSettings(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT)
{
    ++settingsLaunches;
    return reinterpret_cast<HINSTANCE>(33);
}
}

// Only VLC configuration/ML initialization and persistence are replaced. The
// production association actions, button wiring, task ownership, completion,
// and close protection below are exercised without real defaults. Retain the
// OS-launch trap to detect regressions without actually opening Settings.
FirstRunWizard::FirstRunWizard(qt_intf_t *intf, QWidget *parent)
    : QWizard(parent), p_intf(intf)
{
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoCancelButton);
    for (int id = WELCOME_PAGE; id <= DEFAULT_APPS_PAGE; ++id)
        setPage(id, new QWizardPage(this));
    setStartId(DEFAULT_APPS_PAGE);
    setupDefaultAppsPage();
}

void FirstRunWizard::finish()
{
    ++finishes;
    if (finishHook)
        finishHook();
}

void FirstRunWizard::reject() { done(QDialog::Rejected); }
int FirstRunWizard::nextId() const
{
    return currentId() == LAYOUT_PAGE ? DEFAULT_APPS_PAGE : -1;
}
void FirstRunWizard::initializePage(int) {}
void FirstRunWizard::addDefaults() {}
void FirstRunWizard::MLaddNewFolder() {}
void FirstRunWizard::updateColorLabel(QAbstractButton *) {}
void FirstRunWizard::updateLayoutLabel(QAbstractButton *) {}
void FirstRunWizard::imageColorSchemeClick(QAbstractButton *) {}
void FirstRunWizard::imageLayoutClick(QAbstractButton *) {}

// A live interface is unnecessary in this fixture. Leave production logging
// intact, but capture warnings rather than passing a null VLC object to it.
#undef msg_Dbg
#undef msg_Warn
#define msg_Dbg(...) ((void)0)
#define msg_Warn(object, ...) testWarn(__VA_ARGS__)
#define ShellExecuteW testLaunchSettings
#include "../dialogs/firstrun/firstrunwizard_associations.cpp"
#undef ShellExecuteW
#undef msg_Dbg
#undef msg_Warn

extern "C" const char vlc_module_name[] = "firstrun-association-test";

namespace vlc::win32
{
AssociationResult setDefaultAssociation(std::wstring_view extension) noexcept
{
    if (QThread::currentThread() == qApp->thread())
        offUiThread = false;
    if (pauseWrite.exchange(false))
    {
        entered.release();
        resume.tryAcquire(1, 5000); // Bounded even after a failed assertion.
    }
    const std::lock_guard<std::mutex> lock(backendMutex);
    writes.emplace_back(extension);
    const auto found = results.find(std::wstring(extension));
    return found == results.end() ? AssociationResult{AssociationStatus::changed, {}}
                                 : found->second;
}
bool isDefaultAssociation(std::wstring_view) noexcept { return false; }
}

class TestWin32FirstRun : public QObject
{
    Q_OBJECT

    static std::vector<std::wstring> appliedExtensions()
    {
        const std::lock_guard<std::mutex> lock(backendMutex);
        return writes;
    }

    static QMessageBox *notice()
    {
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget))
                return box;
        return nullptr;
    }

    static void verifyFailureNotice(const QMessageBox *message, bool partial,
                                    bool needsConfirmation)
    {
        QCOMPARE(message->icon(), QMessageBox::Warning);
        QCOMPARE(message->textFormat(), Qt::PlainText);
        QString expected = partial
            ? QStringLiteral("VLC could not be made the default for some file types.")
            : QStringLiteral("VLC could not be made your default media player.");
        if (needsConfirmation)
            expected += QStringLiteral(
                "\nYou can change defaults manually in Windows Settings > Apps > Default apps.");
        QCOMPARE(message->text(), expected);
        QCOMPARE(message->standardButtons(), QMessageBox::Ok);
        QCOMPARE(message->buttons().size(), 1);
        QVERIFY(message->informativeText().isEmpty());
        QVERIFY(message->detailedText().isEmpty());
        QCOMPARE(settingsLaunches, 0U);
    }

private slots:
    void init()
    {
        writes.clear();
        results.clear();
        warnings.clear();
        pauseWrite = false;
        offUiThread = true;
        finishes = settingsLaunches = 0;
        finishHook = {};
        entered.tryAcquire(entered.available());
        resume.tryAcquire(resume.available());
        qApp->setQuitOnLastWindowClosed(false);
        qApp->setProperty("isDying", false);
    }

    void cleanup()
    {
        resume.release();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(6000));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        for (QWidget *widget : QApplication::topLevelWidgets())
            if (auto *box = qobject_cast<QMessageBox *>(widget))
                box->accept();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(notice() == nullptr);
        qApp->setProperty("isDying", false);
    }

    void notNow()
    {
        FirstRunWizard wizard(nullptr);
        wizard.show();
        QSignalSpy accepted(&wizard, &QDialog::accepted);
        wizard.button(QWizard::CustomButton1)->click();
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(finishes, 1U);
        QVERIFY(!wizard.isVisible());
        QVERIFY(appliedExtensions().empty());
        QCOMPARE(settingsLaunches, 0U);
        QVERIFY(notice() == nullptr);
        wizard.accept();
        wizard.done(QDialog::Accepted);
        QCOMPARE(finishes, 1U);
        QCOMPARE(accepted.count(), 1);
    }

    void earlierPageAndBack()
    {
        FirstRunWizard wizard(nullptr);
        wizard.setStartId(FirstRunWizard::LAYOUT_PAGE);
        wizard.show();
        QVERIFY(!wizard.button(QWizard::CustomButton1)->isVisible());
        wizard.customButtonClicked(QWizard::CustomButton1);
        QCOMPARE(finishes, 0U);
        wizard.next();
        QCOMPARE(wizard.currentId(), int(FirstRunWizard::DEFAULT_APPS_PAGE));
        QCOMPARE(wizard.button(QWizard::FinishButton)->text(), QStringLiteral("Set as default"));
        QCOMPARE(wizard.button(QWizard::CustomButton1)->text(), QStringLiteral("Not now"));
        QVERIFY(wizard.button(QWizard::CustomButton1)->isVisible());
        QVERIFY(wizard.button(QWizard::FinishButton)->styleSheet().contains(
            QStringLiteral("QPushButton:focus { outline: none; border-color:")));
        wizard.button(QWizard::CustomButton1)->setFocus();
        QTest::keyClick(wizard.button(QWizard::CustomButton1), Qt::Key_Tab);
        // Window-local focus does not depend on Windows granting activation.
        QCOMPARE(wizard.focusWidget(), wizard.button(QWizard::FinishButton));
        wizard.back();
        QCOMPARE(wizard.currentId(), int(FirstRunWizard::LAYOUT_PAGE));
        QVERIFY(wizard.button(QWizard::FinishButton)->styleSheet().isEmpty());
        QVERIFY(appliedExtensions().empty());
    }

    void successfulBatch_data()
    {
        QTest::addColumn<bool>("alreadyDefault");
        QTest::newRow("changed") << false;
        QTest::newRow("unchanged") << true;
    }

    void successfulBatch()
    {
        QFETCH(bool, alreadyDefault);
        const auto extensions = vlc::win32::allMediaExtensions();
        if (alreadyDefault)
            for (const auto &extension : extensions)
                results[extension] = {vlc::win32::AssociationStatus::unchanged, {}};
        FirstRunWizard wizard(nullptr);
        wizard.show();
        QSignalSpy accepted(&wizard, &QDialog::accepted);
        wizard.button(QWizard::FinishButton)->click();
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(finishes, 1U);
        QVERIFY(offUiThread.load());
        QVERIFY(appliedExtensions() == extensions);
        QCOMPARE(settingsLaunches, 0U);
        QVERIFY(notice() == nullptr);
        QVERIFY(!wizard.isVisible());
        QVERIFY(wizard.cursor().shape() != Qt::WaitCursor);
    }

    void inFlightCloseAndDuplicateApply()
    {
        FirstRunWizard wizard(nullptr);
        wizard.show();
        QSignalSpy accepted(&wizard, &QDialog::accepted);
        QSignalSpy rejected(&wizard, &QDialog::rejected);
        pauseWrite = true;
        wizard.button(QWizard::FinishButton)->click();
        QVERIFY(entered.tryAcquire(1, 1000));
        QVERIFY(!wizard.button(QWizard::FinishButton)->isEnabled());
        QVERIFY(!wizard.button(QWizard::CustomButton1)->isEnabled());
        QVERIFY(!wizard.button(QWizard::BackButton)->isEnabled());
        wizard.accept();
        wizard.reject();
        wizard.done(QDialog::Accepted);
        wizard.done(QDialog::Rejected);
        wizard.customButtonClicked(QWizard::CustomButton1);
        QTest::keyClick(&wizard, Qt::Key_Escape);
        wizard.close();
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(finishes, 0U);
        QVERIFY(wizard.isVisible());
        resume.release();
        QTRY_COMPARE(accepted.count(), 1);
        QVERIFY(appliedExtensions() == vlc::win32::allMediaExtensions());
        QCOMPARE(finishes, 1U);
    }

    void partialFailureAndRetry()
    {
        auto parent = std::make_unique<QWidget>();
        FirstRunWizard wizard(nullptr, parent.get());
        wizard.show();
        const auto extensions = vlc::win32::allMediaExtensions();
        for (unsigned index = 0; index < 7; ++index)
            results[extensions[index]] = {vlc::win32::AssociationStatus::failed,
                                          "Failure <not markup>"};
        QSignalSpy accepted(&wizard, &QDialog::accepted);
        wizard.accept();
        QTRY_COMPARE(accepted.count(), 1);
        QTRY_VERIFY(notice() != nullptr);
        const QPointer<QMessageBox> message = notice();
        verifyFailureNotice(message, true, false);
        QCOMPARE(warnings.size(), 7);
        for (unsigned index = 0; index < 7; ++index)
            QVERIFY(warnings[index].contains(QString::fromStdWString(extensions[index])));
        message->accept();
        QTRY_VERIFY(message.isNull());

        // DialogsProvider retains the wizard: reuse cannot retain disabled
        // buttons or skip a retry merely because a previous attempt completed.
        results.clear();
        wizard.show();
        wizard.accept();
        QTRY_COMPARE(accepted.count(), 2);
        QCOMPARE(finishes, 2U);
        QCOMPARE(appliedExtensions().size(), extensions.size() * 2);
        QVERIFY(notice() == nullptr);
    }

    void incompleteBatch_data()
    {
        QTest::addColumn<bool>("hasFailure");
        QTest::newRow("confirmation") << false;
        QTest::newRow("mixed") << true;
    }

    void incompleteBatch()
    {
        QFETCH(bool, hasFailure);
        const auto extensions = vlc::win32::allMediaExtensions();
        results[extensions[0]] = {vlc::win32::AssociationStatus::needsUserChoice, "Confirmation reason"};
        if (hasFailure)
            results[extensions[1]] = {vlc::win32::AssociationStatus::failed, "Failure reason"};
        FirstRunWizard wizard(nullptr);
        wizard.show();
        QSignalSpy accepted(&wizard, &QDialog::accepted);
        wizard.accept();
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(finishes, 1U);
        QTRY_VERIFY(notice() != nullptr);
        verifyFailureNotice(notice(), true, true);
        QCOMPARE(warnings.size(), hasFailure ? 2 : 1);
        QVERIFY(warnings[0].contains(QStringLiteral("Confirmation reason")));
        if (hasFailure)
            QVERIFY(warnings[1].contains(QStringLiteral("Failure reason")));
        QVERIFY(!wizard.isVisible());
        QVERIFY(appliedExtensions() == extensions);
    }

    void allFailed_data()
    {
        QTest::addColumn<bool>("needsConfirmation");
        QTest::newRow("failed") << false;
        QTest::newRow("confirmation") << true;
    }

    void allFailed()
    {
        QFETCH(bool, needsConfirmation);
        for (const auto &extension : vlc::win32::allMediaExtensions())
            results[extension] = {needsConfirmation
                ? vlc::win32::AssociationStatus::needsUserChoice
                : vlc::win32::AssociationStatus::failed, "Failure reason"};
        FirstRunWizard wizard(nullptr);
        wizard.show();
        wizard.accept();
        QTRY_COMPARE(finishes, 1U);
        QTRY_VERIFY(notice() != nullptr);
        verifyFailureNotice(notice(), false, needsConfirmation);
        QVERIFY(!wizard.isVisible());
    }

    void destructionDuringApply()
    {
        auto wizard = std::make_unique<FirstRunWizard>(nullptr);
        wizard->show();
        for (const auto &extension : vlc::win32::allMediaExtensions())
            results[extension] = {vlc::win32::AssociationStatus::needsUserChoice, "Abandoned"};
        pauseWrite = true;
        wizard->accept();
        QVERIFY(entered.tryAcquire(1, 1000));
        wizard.reset();
        resume.release();
        QVERIFY(QThreadPool::globalInstance()->waitForDone(6000));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(finishes, 0U);
        QCOMPARE(settingsLaunches, 0U);
        QVERIFY(notice() == nullptr);
        QVERIFY(appliedExtensions() == vlc::win32::allMediaExtensions());
    }

    void shutdownDuringApply()
    {
        FirstRunWizard wizard(nullptr);
        wizard.show();
        results[L".mp4"] = {vlc::win32::AssociationStatus::needsUserChoice, "Shutdown"};
        results[L".mp3"] = {vlc::win32::AssociationStatus::failed, "Shutdown"};
        pauseWrite = true;
        wizard.accept();
        QVERIFY(entered.tryAcquire(1, 1000));
        qApp->setProperty("isDying", true);
        resume.release();
        QTRY_VERIFY(wizard.button(QWizard::FinishButton)->isEnabled());
        QCOMPARE(finishes, 0U);
        QCOMPARE(settingsLaunches, 0U);
        QVERIFY(notice() == nullptr);
        QVERIFY(appliedExtensions() == vlc::win32::allMediaExtensions());
    }

    void shutdownFromAccepted()
    {
        FirstRunWizard wizard(nullptr);
        wizard.show();
        results[L".mp4"] = {vlc::win32::AssociationStatus::failed, "Shutdown"};
        connect(&wizard, &QDialog::accepted, &wizard, [] {
            qApp->setProperty("isDying", true);
        });
        wizard.accept();
        QTRY_COMPARE(finishes, 1U);
        QTRY_VERIFY(notice() == nullptr);
        QCOMPARE(settingsLaunches, 0U);
    }

    void destructionFromAccepted_data()
    {
        QTest::addColumn<bool>("hasFailure");
        QTest::newRow("quiet-success") << false;
        QTest::newRow("failure-notice") << true;
    }

    void destructionFromAccepted()
    {
        QFETCH(bool, hasFailure);
        auto parent = std::make_unique<QWidget>();
        const QPointer<FirstRunWizard> wizard = new FirstRunWizard(nullptr, parent.get());
        if (hasFailure)
            results[L".mp4"] = {vlc::win32::AssociationStatus::failed, "Owned notice"};
        wizard->show();
        connect(wizard, &QDialog::accepted, wizard, [wizard] { delete wizard; });
        wizard->accept();
        QTRY_VERIFY(wizard.isNull());
        QCOMPARE(finishes, 1U);
        if (hasFailure)
        {
            QTRY_VERIFY(notice() != nullptr);
            const QPointer<QMessageBox> message = notice();
            parent.reset();
            QVERIFY(message.isNull());
        }
    }

    void destructionDuringFinish()
    {
        const QPointer<FirstRunWizard> wizard = new FirstRunWizard(nullptr);
        wizard->show();
        finishHook = [wizard] { delete wizard; };
        wizard->button(QWizard::CustomButton1)->click();
        QVERIFY(wizard.isNull());
        QCOMPARE(finishes, 1U);
        QVERIFY(appliedExtensions().empty());
    }

    void destructionDuringFailedFinish()
    {
        auto parent = std::make_unique<QWidget>();
        const QPointer<FirstRunWizard> wizard = new FirstRunWizard(nullptr, parent.get());
        wizard->show();
        results[L".mp4"] = {vlc::win32::AssociationStatus::needsUserChoice, "Confirmation"};
        finishHook = [wizard] { delete wizard; };
        wizard->accept();
        QTRY_VERIFY(wizard.isNull());
        QCOMPARE(finishes, 1U);
        QTRY_VERIFY(notice() != nullptr);
        verifyFailureNotice(notice(), true, true);
        const QPointer<QMessageBox> message = notice();
        parent.reset();
        QVERIFY(message.isNull());
    }

    void reentrantCompletion()
    {
        FirstRunWizard wizard(nullptr);
        wizard.show();
        QSignalSpy accepted(&wizard, &QDialog::accepted);
        finishHook = [&wizard] { wizard.accept(); wizard.done(QDialog::Accepted); };
        connect(&wizard, &QDialog::accepted, &wizard, [&wizard] {
            wizard.accept();
            wizard.done(QDialog::Accepted);
            wizard.customButtonClicked(QWizard::CustomButton1);
        });
        wizard.accept();
        QTRY_COMPARE(accepted.count(), 1);
        QCOMPARE(finishes, 1U);
        QVERIFY(appliedExtensions() == vlc::win32::allMediaExtensions());
    }
};

#ifdef QT_STATIC
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif
QTEST_MAIN(TestWin32FirstRun)
#include "test_win32_firstrun.moc"
