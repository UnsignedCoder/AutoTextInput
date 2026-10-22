/**
 * @file main.cpp
 * @brief Application entry point: environment setup, the single-instance check, and the Qt event loop.
 */

#include "Platform.h"
#include "TrayController.h"

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStringList>

/**
 * @brief Application entry point.
 * @param argc Argument count, forwarded to QApplication (which consumes Qt-specific options like -platform).
 * @param argv Argument values. Pass `--show` to open the main window immediately at launch.
 * @return The exit code passed to QApplication::exit(), returned from QApplication::exec(). 0 on a normal quit.
 *
 * @details Startup sequence:
 *          1. Platform environment setup, which must happen before Qt initializes.
 *          2. QApplication creation and identity (organization/app name, used by QSettings).
 *          3. Single-instance check: a second copy couldn't register the same global shortcuts.
 *          4. TrayController creation, which builds the whole app: tray icon, hotkeys, window.
 *          5. The event loop (exec()) runs until Quit is chosen.
 *
 *          No window is shown at launch, only the tray icon. Windows 11 puts new tray icons under "Show
 *          hidden icons" by default.
 */
int main (int argc, char* argv[]) {
    Platform::PrepareEnvironment();

    QApplication App (argc, argv);
    QApplication::setOrganizationName("UnsignedCoder");
    QApplication::setApplicationName("AutoTextInput");
    // Qt normally quits when the last window closes. This app lives in the tray, so closing (hiding) the
    // window must not end it.
    QApplication::setQuitOnLastWindowClosed(false);

    // Single-instance guard. QLockFile creates a file holding this process's PID. tryLock(0) fails straight
    // away if another *running* process holds the lock. If the previous holder crashed, QLockFile sees that
    // its PID is gone, treats the lock as stale and takes it over. The lock lives in the per-user app data
    // folder, so two different users on the same machine can each run their own copy.
    const QString lockDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(lockDirectory);
    QLockFile instanceLock(QDir(lockDirectory).filePath("instance.lock"));
    if (!instanceLock.tryLock(0)) {
        QMessageBox::information(nullptr, "AutoTextInput",
                                 "AutoTextInput is already running.\n\n"
                                 "Look for its icon in the system tray / notification area.");
        return 0;
    }

    // The controller lives on main()'s stack for the whole event loop, and its destructor tears everything
    // down in order when exec() returns.
    TrayController Controller;
    if (App.arguments().contains("--show")) {
        Controller.ShowWindow();
    }

    return App.exec();
    // instanceLock's destructor releases and deletes the lock file here.
}
