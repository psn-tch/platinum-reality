// platinum-lock without a compositor: PAM through a disposable configuration directory (never
// the system's), the Log In dialog driven by real key events, and fingerprint unlock against a
// fake fprintd on a private bus. The Wayland lock itself is qualified by lock_session_tests.py.
#include "../src/lock/authenticator.hpp"
#include "../src/lock/fingerprint.hpp"
#include "../src/lock/lock_controller.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMutex>
#include <QProcess>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <optional>

namespace
{
bool write(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

void type(QWindow *window, const QString &text)
{
    for (const auto character : text)
        QTest::keyClick(window, character.toLatin1());
}

// A disposable PAM directory: the password is "correct horse", checked by a script through
// pam_exec (which asks the conversation for it), after an informational message from pam_echo.
struct TestPam
{
    QTemporaryDir directory;
    bool ready() const { return valid; }
    QString confdir() const { return directory.path(); }
    bool valid = false;
    TestPam()
    {
        const auto script = directory.filePath("check-password");
        // pam_exec writes the token followed by a NUL.
        valid = write(script, "#!/bin/bash\nIFS= read -r -d '' password\n[ \"$password\" = 'correct horse' ]\n") &&
                QFile::setPermissions(script, QFile::ReadOwner | QFile::ExeOwner) &&
                write(directory.filePath("platinum-lock-test"),
                      "auth optional pam_echo.so Checking with the test service\n"
                      "auth required pam_exec.so expose_authtok quiet " + script.toUtf8() + "\n"
                      "account required pam_permit.so\n");
    }
};

// fprintd's D-Bus surface, enough for one reader and one enrolled finger. It runs on its own
// thread, as the real fprintd is its own process: the lock's blocking Release must be answered.
class FakeDevice final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.reactivated.Fprint.Device")
  public:
    QStringList calls() const
    {
        QMutexLocker lock(&m_mutex);
        return m_calls;
    }
    bool claimed() const
    {
        QMutexLocker lock(&m_mutex);
        return m_claimed;
    }
    void reset()
    {
        QMutexLocker lock(&m_mutex);
        m_calls.clear();
    }
  public slots:
    QStringList ListEnrolledFingers(const QString &user)
    {
        record("ListEnrolledFingers " + user);
        return {"right-index-finger"};
    }
    void Claim(const QString &user) { record("Claim " + user, true); }
    void VerifyStart(const QString &finger) { record("VerifyStart " + finger); }
    void VerifyStop() { record("VerifyStop"); }
    void Release() { record("Release", false); }

  private:
    void record(const QString &call, std::optional<bool> claim = {})
    {
        QMutexLocker lock(&m_mutex);
        m_calls << call;
        if (claim)
            m_claimed = *claim;
    }
    mutable QMutex m_mutex;
    QStringList m_calls;
    bool m_claimed = false;
};
class FakeManager final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.reactivated.Fprint.Manager")
  public slots:
    QDBusObjectPath GetDefaultDevice() { return QDBusObjectPath("/net/reactivated/Fprint/Device/0"); }
};
} // namespace

class LockTests final : public QObject
{
    Q_OBJECT
    QProcess bus;
    QString address;
    bool busReady()
    {
        if (!address.isEmpty())
            return true;
        bus.start("dbus-daemon", {"--session", "--nofork", "--print-address"});
        if (!bus.waitForStarted() || !bus.waitForReadyRead(5000))
            return false;
        address = QString::fromUtf8(bus.readLine()).trimmed();
        return !address.isEmpty();
    }

  private slots:
    void cleanupTestCase()
    {
        if (bus.state() != QProcess::NotRunning)
        {
            bus.terminate();
            bus.waitForFinished(3000);
        }
    }

    void fingerprintIsOfferedOnlyWhereThePamStackAllowsIt()
    {
        QTemporaryDir pam;
        QVERIFY(write(pam.filePath("system-auth"), "#%PAM-1.0\nauth required pam_faillock.so preauth\n"
                                                   "auth sufficient pam_fprintd.so\n"
                                                   "auth [success=1 default=bad] pam_unix.so try_first_pass\n"));
        QVERIFY(write(pam.filePath("login"), "auth include system-auth\n"));
        QVERIFY(write(pam.filePath("swaylock"), "auth include login\n"));
        QVERIFY(pamServiceUsesFingerprint("swaylock", pam.path())); // psn_tch's chain, three deep.
        QVERIFY(write(pam.filePath("system-auth"), "#auth sufficient pam_fprintd.so\n"
                                                   "auth [success=1 default=bad] pam_unix.so\n"
                                                   "account required pam_fprintd.so\n"));
        QVERIFY(!pamServiceUsesFingerprint("swaylock", pam.path())); // Commented out, or not auth.
        QVERIFY(write(pam.filePath("debian"), "@include common\n"));
        QVERIFY(write(pam.filePath("common"), "-auth [default=ignore] pam_fprintd.so max-tries=1\n"));
        QVERIFY(pamServiceUsesFingerprint("debian", pam.path()));
        QVERIFY(write(pam.filePath("loop-a"), "auth substack loop-b\n"));
        QVERIFY(write(pam.filePath("loop-b"), "auth include loop-a\n"));
        QVERIFY(!pamServiceUsesFingerprint("loop-a", pam.path())); // A cycle ends.
        QVERIFY(write(pam.filePath("escape"), "auth include ../common\n"));
        QVERIFY(!pamServiceUsesFingerprint("escape", pam.path())); // Never outside the directory.
        QVERIFY(!pamServiceUsesFingerprint("absent", pam.path()));
    }

    void theDesktopPictureComesFromTheAppearancePanel()
    {
        QTemporaryDir home;
        const auto profile = home.filePath("profile");
        QVERIFY(QDir().mkpath(profile) && QDir().mkpath(home.filePath("pictures")));
        QVERIFY(write(home.filePath("pictures/room.png"), "not checked here"));
        QCOMPARE(desktopPictureFrom(profile, home.path()), QUrl()); // No settings: plain color.
        QVERIFY(write(profile + "/appearance.json", R"({"wallpaperPath": "~/pictures/room.png", "wallpaperLayout": "fill"})"));
        QCOMPARE(desktopPictureFrom(profile, home.path()), QUrl::fromLocalFile(home.filePath("pictures/room.png")));
        QVERIFY(write(profile + "/appearance.json", R"({"wallpaperPath": "pictures/room.png"})"));
        QCOMPARE(desktopPictureFrom(profile, home.path()), QUrl()); // Never relative to anything.
        QVERIFY(write(profile + "/appearance.json", R"({"wallpaperPath": ""})"));
        QCOMPARE(desktopPictureFrom(profile, home.path()), QUrl()); // "No picture" chosen.
        QVERIFY(write(profile + "/appearance.json", "{" + QByteArray(1024 * 1024, ' ') + "}"));
        QCOMPARE(desktopPictureFrom(profile, home.path()), QUrl());
        QCOMPARE(userFullName("someone", ""), QString("someone"));
        QCOMPARE(userFullName("psn_tch", "Pat Example,,,"), QString("Pat Example"));
    }

    void passwordsAreCheckedByPamOffTheUiThread()
    {
        TestPam pam;
        QVERIFY(pam.ready());
        Authenticator authenticator("platinum-lock-test", "tester", pam.confdir());
        QSignalSpy finished(&authenticator, &Authenticator::finished);
        QSignalSpy messages(&authenticator, &Authenticator::message);
        QVERIFY(authenticator.submit("wrong horse"));
        QVERIFY(!authenticator.submit("correct horse")); // One attempt at a time.
        QVERIFY(authenticator.busy());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst().first().toBool(), false);
        QVERIFY(!authenticator.busy());
        QVERIFY(authenticator.submit("correct horse"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.takeFirst().first().toBool(), true);
        QVERIFY(!messages.isEmpty());
        QCOMPARE(messages.first().at(0).toString(), QString("Checking with the test service"));
        QCOMPARE(messages.first().at(1).toBool(), false);
        // An unknown service is denied, never allowed.
        Authenticator missing("no-such-service", "tester", pam.confdir());
        QSignalSpy denied(&missing, &Authenticator::finished);
        QVERIFY(missing.submit("correct horse"));
        QTRY_COMPARE(denied.size(), 1);
        QCOMPARE(denied.first().first().toBool(), false);
    }

    void logInDialogTakesTypedPasswords()
    {
        TestPam pam;
        QVERIFY(pam.ready());
        Authenticator authenticator("platinum-lock-test", "tester", pam.confdir());
        LockController controller(&authenticator, "psn_tch", {}, {});
        QSignalSpy authenticated(&controller, &LockController::authenticated);
        QSignalSpy rejected(&controller, &LockController::rejected);
        QQuickView view;
        view.rootContext()->setContextProperty("lock", &controller);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1024, 700);
        view.setSource(QUrl("qrc:/lock/src/lock/LockScreen.qml"));
        QCOMPARE(view.status(), QQuickView::Ready);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject();
        auto *password = root->findChild<QQuickItem *>("login-password");
        auto *message = root->findChild<QQuickItem *>("login-message");
        auto *hint = root->findChild<QQuickItem *>("login-fingerprint-hint");
        QVERIFY(password && message && hint);
        QCOMPARE(root->findChild<QQuickItem *>("login-name")->property("text").toString(), QString("Welcome, psn_tch"));
        QTRY_VERIFY(password->hasActiveFocus());

        // Before the screen is locked nothing can be submitted.
        type(&view, "correct horse");
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::qWait(200);
        QVERIFY(authenticated.isEmpty());
        QCOMPARE(password->property("text").toString(), QString());
        controller.setReady(true);

        type(&view, "wrong horse");
        QCOMPARE(password->property("text").toString(), QString("wrong horse"));
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(password->property("text").toString(), QString()); // Cleared once submitted.
        QTRY_COMPARE(rejected.size(), 1);
        QCOMPARE(message->property("text").toString(), QString("The password is incorrect. Please try again."));
        QCOMPARE(controller.failures(), 1);
        QVERIFY(authenticated.isEmpty());
        QTRY_VERIFY(password->hasActiveFocus());

        QVERIFY(!hint->isVisible());
        controller.setFingerprintAvailable(true);
        QVERIFY(hint->isVisible());

        type(&view, "correct horse");
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(authenticated.size(), 1);
        QCOMPARE(message->property("text").toString(), QString("Logging in…"));
    }

    void fingerprintUnlocksOnlyOnTheReadersOwnMatch()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto service = QDBusConnection::connectToBus(address, "fake-fprintd");
        auto client = QDBusConnection::connectToBus(address, "lock-client");
        auto impostor = QDBusConnection::connectToBus(address, "impostor");
        QVERIFY(service.isConnected() && client.isConnected() && impostor.isConnected());
        QThread fprintd;
        fprintd.start();
        const auto stopFprintd = qScopeGuard([&] { fprintd.quit(); fprintd.wait(); });
        FakeManager manager;
        FakeDevice device;
        manager.moveToThread(&fprintd);
        device.moveToThread(&fprintd);
        QVERIFY(service.registerObject("/net/reactivated/Fprint/Manager", &manager, QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerObject("/net/reactivated/Fprint/Device/0", &device, QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService("net.reactivated.Fprint"));
        const auto status = [](const QString &result, bool done)
        {
            auto signal = QDBusMessage::createSignal("/net/reactivated/Fprint/Device/0",
                                                     "net.reactivated.Fprint.Device", "VerifyStatus");
            signal << result << done;
            return signal;
        };
        {
            Fingerprint fingerprint("tester", client);
            QSignalSpy available(&fingerprint, &Fingerprint::availableChanged);
            QSignalSpy matched(&fingerprint, &Fingerprint::matched);
            QSignalSpy hints(&fingerprint, &Fingerprint::status);
            fingerprint.start();
            QTRY_COMPARE(available.size(), 1);
            QVERIFY(fingerprint.available());
            QCOMPARE(device.calls(), QStringList({"ListEnrolledFingers tester", "Claim tester", "VerifyStart any"}));

            // Someone else on the bus cannot report a match.
            QVERIFY(impostor.send(status("verify-match", true)));
            QTest::qWait(300);
            QVERIFY(matched.isEmpty());

            // A wrong finger: told, and the reader starts verifying again.
            QVERIFY(service.send(status("verify-no-match", true)));
            QTRY_COMPARE(hints.size(), 1);
            QCOMPARE(hints.first().first().toString(), QString("That fingerprint was not recognized. Try again."));
            QTRY_COMPARE(device.calls().mid(3), QStringList({"VerifyStop", "VerifyStart any"}));
            QVERIFY(matched.isEmpty());

            QVERIFY(service.send(status("verify-match", true)));
            QTRY_COMPARE(matched.size(), 1);
            fingerprint.stop();
            QVERIFY(!device.claimed());
            QCOMPARE(device.calls().last(), QString("Release"));
        }
        // Without fprintd on the bus, fingerprint unlock is simply not offered.
        device.reset();
        QVERIFY(service.unregisterService("net.reactivated.Fprint"));
        Fingerprint none("tester", client);
        QSignalSpy available(&none, &Fingerprint::availableChanged);
        none.start();
        QTest::qWait(500);
        QVERIFY(available.isEmpty() && !none.available());
        QVERIFY(device.calls().isEmpty());
        service.unregisterObject("/net/reactivated/Fprint/Manager");
        service.unregisterObject("/net/reactivated/Fprint/Device/0");
    }
};

QTEST_MAIN(LockTests)
#include "lock_tests.moc"
