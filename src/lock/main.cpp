// platinum-lock: the Platinum Reality screen lock, with the same contract as the swaylock it
// replaces in the session supervisor: "\n" on --ready-fd once every output is locked, and exit
// status 0 only after a successful unlock. Any other exit leaves the compositor locked.
#include "authenticator.hpp"
#include "fingerprint.hpp"
#include "lock_controller.hpp"
#include "session_lock.hpp"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScreen>
#include <cstdlib>
#include <memory>
#include <pwd.h>
#include <unistd.h>
#include <vector>

int main(int argc, char **argv)
{
    // The lock is a Wayland protocol role; there is no lock on any other platform.
    qputenv("QT_QPA_PLATFORM", "wayland");
    QGuiApplication app(argc, argv);
    app.setApplicationName("platinum-lock");
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption readyFd("ready-fd", "Write a newline here once locked.", "fd");
    const QCommandLineOption service("pam-service", "PAM service for passwords.", "name", "swaylock");
    const QCommandLineOption confdir("pam-confdir", "Qualification only: a disposable PAM directory.", "dir");
    const QCommandLineOption fingerprintBus("fingerprint-bus",
                                            "Qualification only: 'session' for a fake fprintd.", "bus", "system");
    const QCommandLineOption profile("profile", "The desktop profile whose picture is shown behind the dialog.",
                                     "dir");
    // Log In at the start of a session: also choose the session, reported on --session-fd as
    // "platinum\n" or "kde\n" only after a successful authentication.
    const QCommandLineOption login("login", "Log In before the session starts, with a session choice.");
    const QCommandLineOption defaultSession("default-session", "The session chosen at first: platinum or kde.",
                                            "name", "platinum");
    const QCommandLineOption sessionFd("session-fd", "Write the chosen session here after Log In.", "fd");
    parser.addOptions({readyFd, service, confdir, fingerprintBus, profile, login, defaultSession, sessionFd});
    parser.process(app);

    const auto *account = getpwuid(getuid());
    if (!account || !account->pw_name)
        qFatal("No password database entry for this user");
    const QString user = QString::fromLocal8Bit(account->pw_name);
    int ready = -1;
    if (parser.isSet(readyFd))
    {
        bool ok = false;
        ready = parser.value(readyFd).toInt(&ok);
        if (!ok || ready < 0)
            qFatal("Invalid --ready-fd");
    }
    int chosen = -1;
    if (parser.isSet(sessionFd))
    {
        bool ok = false;
        chosen = parser.value(sessionFd).toInt(&ok);
        if (!ok || chosen < 0 || !parser.isSet(login))
            qFatal("Invalid --session-fd (it needs --login)");
    }

    SessionLock lock;
    if (!lock.available())
    {
        qCritical("The compositor does not offer ext-session-lock-v1");
        return 3;
    }
    Authenticator authenticator(parser.value(service), user, parser.value(confdir));
    const auto home = QString::fromLocal8Bit(account->pw_dir ? account->pw_dir : "");
    // The launcher's default profile ($XDG_DATA_HOME/platinum-reality/profile).
    const auto data = qEnvironmentVariable("XDG_DATA_HOME", home + "/.local/share");
    LockController controller(&authenticator,
                              userFullName(user, QString::fromLocal8Bit(account->pw_gecos ? account->pw_gecos : "")),
                              userPicture(user, home),
                              desktopPictureFrom(parser.isSet(profile) ? parser.value(profile)
                                                                       : data + "/platinum-reality/profile",
                                                 home));
    if (parser.isSet(login))
        controller.setLoginMode(parser.value(defaultSession));
    // Only where the system already allows fingerprint login (pam_fprintd in the stack).
    const bool fingerprintAllowed = pamServiceUsesFingerprint(parser.value(service), parser.value(confdir));
    Fingerprint fingerprint(user, parser.value(fingerprintBus) == "session" ? QDBusConnection::sessionBus()
                                                                              : QDBusConnection::systemBus());
    QObject::connect(&fingerprint, &Fingerprint::availableChanged, &controller,
                     &LockController::setFingerprintAvailable);
    QObject::connect(&fingerprint, &Fingerprint::status, &controller,
                     [&controller](const QString &text) { controller.showStatus(text, true); });

    std::vector<std::unique_ptr<QQuickView>> views;
    const auto cover = [&](QScreen *screen)
    {
        auto view = std::make_unique<QQuickView>();
        // Qt moves a window to whichever screen contains its position: place it on its own.
        view->setScreen(screen);
        view->setGeometry(screen->geometry());
        view->setColor(QColor("#666699"));
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->rootContext()->setContextProperty("lock", &controller);
        view->setSource(QUrl("qrc:/lock/src/lock/LockScreen.qml"));
        if (view->status() != QQuickView::Ready || !lock.cover(view.get()))
        {
            qCritical("Cannot cover screen %s", qPrintable(screen->name()));
            return;
        }
        view->show();
        views.push_back(std::move(view));
    };
    if (!lock.lock())
        return 3;
    for (auto *screen : QGuiApplication::screens())
        cover(screen);
    // Every window here is a lock window: the keyboard is on the lock when any has focus.
    QObject::connect(&app, &QGuiApplication::focusWindowChanged, &controller,
                     [&controller](QWindow *window) { controller.setKeyboardOnLock(window != nullptr); });
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, cover);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app,
                     [&views](QScreen *screen)
                     {
                         std::erase_if(views, [screen](const auto &view) { return view->screen() == screen; });
                     });

    QObject::connect(&lock, &SessionLock::locked, &app,
                     [&]
                     {
                         if (ready >= 0)
                         {
                             (void)!write(ready, "\n", 1);
                             close(ready);
                             ready = -1;
                         }
                         controller.setReady(true);
                         if (fingerprintAllowed)
                             fingerprint.start();
                     });
    QObject::connect(&lock, &SessionLock::finished, &app,
                     [&]
                     {
                         // Refused (another locker holds the screen) or ended by the compositor:
                         // never an unlock by this process.
                         qWarning("The compositor ended or refused the lock");
                         app.exit(controller.ready() ? 4 : 5);
                     });
    const auto unlock = [&]
    {
        fingerprint.stop();
        if (!lock.unlock())
            return;
        if (chosen >= 0)
        {
            const auto line = (controller.session() + "\n").toLatin1();
            (void)!write(chosen, line.constData(), size_t(line.size()));
            close(chosen);
            chosen = -1;
        }
        // A finger can win while a password is still inside PAM (a slow module). The screen is
        // already unlocked: do not make the supervisor wait for that module before exit 0.
        if (authenticator.busy())
            std::_Exit(0);
        views.clear();
        app.exit(0);
    };
    QObject::connect(&controller, &LockController::authenticated, &app, unlock);
    QObject::connect(&fingerprint, &Fingerprint::matched, &app, unlock);
    return app.exec();
}
