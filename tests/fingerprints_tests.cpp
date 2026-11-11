#include "appearance/interface_font.hpp"
#include "desktop/fingerprints.hpp"
#include "desktop/manager.hpp"
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QDBusObjectPath>
#include <QDBusVirtualObject>
#include <QMutex>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

// The Fingerprints control panel's service against a fake fprintd on a private bus (its own thread,
// as fprintd is its own process) and a stand-in for the root helper. Nothing reaches the host's
// fprintd, PAM files or pkexec.
namespace
{
const QString devicePath = "/net/reactivated/Fprint/Device/0";
const QString deviceInterface = "net.reactivated.Fprint.Device";

class FakeFprintd final : public QDBusVirtualObject
{
    Q_OBJECT
  public:
    QStringList calls() const
    {
        QMutexLocker lock(&m_mutex);
        return m_calls;
    }
    void reset()
    {
        QMutexLocker lock(&m_mutex);
        m_calls.clear();
    }
    bool claimed() const
    {
        QMutexLocker lock(&m_mutex);
        return m_claimed;
    }
    // The next Claim fails with this fprintd error (for example PermissionDenied: password box dismissed).
    void failNextClaim(const QString &error)
    {
        QMutexLocker lock(&m_mutex);
        m_claimError = error;
    }
    void setReader(bool present)
    {
        QMutexLocker lock(&m_mutex);
        m_reader = present;
    }
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        QMutexLocker lock(&m_mutex);
        const auto method = message.member();
        const auto arguments = message.arguments();
        m_calls << (method + (arguments.isEmpty() ? QString() : " " + arguments.first().toString())).trimmed();
        const auto error = [&](const QString &name)
        { return connection.send(message.createErrorReply("net.reactivated.Fprint.Error." + name, name)); };
        if (method == "GetDefaultDevice")
            return m_reader ? connection.send(message.createReply(QVariant::fromValue(QDBusObjectPath(devicePath))))
                            : error("NoSuchDevice");
        if (method == "GetAll")
            return connection.send(message.createReply(QVariant::fromValue(QVariantMap{
                {"name", "Fake Reader"}, {"num-enroll-stages", 3}, {"scan-type", "press"}})));
        if (method == "ListEnrolledFingers")
            return m_fingers.isEmpty() ? error("NoEnrolledPrints") : connection.send(message.createReply(m_fingers));
        if (method == "Claim")
        {
            if (!m_claimError.isEmpty())
                return error(std::exchange(m_claimError, QString()));
            if (m_claimed)
                return error("AlreadyInUse");
            m_claimed = true;
            return connection.send(message.createReply());
        }
        if (!m_claimed)
            return error("ClaimDevice");
        if (method == "Release")
            m_claimed = false;
        else if (method == "EnrollStart")
            m_enrolling = arguments.first().toString();
        else if (method == "EnrollStop")
            m_enrolling.clear();
        else if (method == "DeleteEnrolledFinger")
            m_fingers.removeAll(arguments.first().toString());
        return connection.send(message.createReply());
    }
    // fprintd adds the finger when the enrolment completes.
    void completeEnrolment()
    {
        QMutexLocker lock(&m_mutex);
        m_fingers << m_enrolling;
    }

  private:
    mutable QMutex m_mutex;
    QStringList m_calls, m_fingers{"left-index-finger"};
    QString m_claimError, m_enrolling;
    bool m_claimed = false, m_reader = true;
};
} // namespace

class FingerprintsTests final : public QObject
{
    Q_OBJECT
    QProcess bus;
    QString address;
    QTemporaryDir directory;
    QThread thread;
    FakeFprintd *fake = nullptr;
    QDBusConnection service{QString()};
    QDBusConnection client{QString()};

    QString helper() const { return directory.filePath("helper.py"); }
    QStringList helperCalls() const
    {
        QFile log(directory.filePath("helper.log"));
        return log.open(QIODevice::ReadOnly) ? QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts)
                                             : QStringList();
    }
    bool status(const QString &result, bool done)
    {
        auto signal = QDBusMessage::createSignal(devicePath, deviceInterface, "EnrollStatus");
        signal << result << done;
        return service.send(signal);
    }

    static QQuickItem *visualItem(QQuickItem *root, const QString &name)
    {
        if (root->objectName() == name)
            return root;
        for (auto *child : root->childItems())
            if (auto *found = visualItem(child, name))
                return found;
        return nullptr;
    }

  private slots:
    void initTestCase()
    {
        QGuiApplication::setFont(classicInterfaceFont());
        bus.start("dbus-daemon", {"--session", "--nofork", "--print-address"});
        if (!bus.waitForStarted() || !bus.waitForReadyRead(5000))
            QSKIP("dbus-daemon is not available");
        address = QString::fromUtf8(bus.readLine()).trimmed();
        service = QDBusConnection::connectToBus(address, "fake-fprintd");
        client = QDBusConnection::connectToBus(address, "panel");
        QVERIFY(service.isConnected() && client.isConnected());
        thread.start();
        fake = new FakeFprintd;
        fake->moveToThread(&thread);
        QVERIFY(service.registerVirtualObject("/net/reactivated/Fprint/Manager", fake));
        QVERIFY(service.registerVirtualObject(devicePath, fake));
        QVERIFY(service.registerService("net.reactivated.Fprint"));
        // The root helper's stand-in: its state in a file, every call logged.
        QFile script(helper());
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("import json, pathlib, sys\n"
                     "here = pathlib.Path(sys.argv[0]).parent\n"
                     "state = here / 'state.json'\n"
                     "s = json.loads(state.read_text()) if state.exists() else "
                     "{'available': True, 'login': False, 'sudo': False}\n"
                     "with open(here / 'helper.log', 'a') as log: log.write(' '.join(sys.argv[1:]) + '\\n')\n"
                     "if sys.argv[1] == 'set':\n"
                     "    s['login'], s['sudo'] = sys.argv[2] == 'on', sys.argv[3] == 'on'\n"
                     "    state.write_text(json.dumps(s))\n"
                     "    print(json.dumps({'backup': '/var/lib/x'})); print(json.dumps({'done': True, **s}))\n"
                     "elif sys.argv[1] == 'install-support':\n"
                     "    print(json.dumps({'error': 'Could not install fprintd (no network).'})); sys.exit(1)\n"
                     "else:\n"
                     "    print(json.dumps(s))\n");
    }
    void cleanupTestCase()
    {
        if (fake)
        {
            service.unregisterObject(devicePath);
            service.unregisterObject("/net/reactivated/Fprint/Manager");
            QMetaObject::invokeMethod(fake, "deleteLater");
        }
        thread.quit();
        thread.wait();
        bus.terminate();
        bus.waitForFinished(3000);
    }
    void init()
    {
        if (fake)
            fake->reset();
    }

    void withoutFprintdNothingIsOffered()
    {
        auto other = QDBusConnection::connectToBus(address, "no-fprintd-here");
        QDBusConnection disconnected{QString()};
        FingerprintService none(disconnected, directory.filePath("missing-helper"), {});
        none.refresh();
        QVERIFY(!none.installed() && none.reader().isEmpty() && !none.loginAvailable() && !none.busy());
        // A bus with no fprintd: the bus answers, not fprintd, so it is not installed.
        service.unregisterService("net.reactivated.Fprint");
        const auto restore = qScopeGuard([&] { QVERIFY(service.registerService("net.reactivated.Fprint")); });
        FingerprintService absent(other, helper(), {});
        absent.refresh();
        QTRY_VERIFY(!absent.busy());
        QVERIFY(!absent.installed());
        QTRY_VERIFY(absent.loginAvailable()); // the PAM module is a separate question, asked of the helper
    }

    void readsTheReaderAndTheUsersFingers()
    {
        FingerprintService fingerprints(client, helper(), {});
        QSignalSpy changed(&fingerprints, &FingerprintService::changed);
        fingerprints.refresh();
        QTRY_VERIFY(!fingerprints.busy() && !fingerprints.changing());
        QVERIFY(fingerprints.installed());
        QCOMPARE(fingerprints.reader(), QString("Fake Reader"));
        QCOMPARE(fingerprints.stages(), 3);
        QCOMPARE(fingerprints.scanType(), QString("press"));
        QCOMPARE(fingerprints.fingers(), QStringList{"left-index-finger"});
        QVERIFY(!fingerprints.login() && !fingerprints.sudo());
        // Only reading: the reader was never claimed. "" is fprintd's "the user calling".
        QVERIFY(!fake->claimed());
        QVERIFY(fake->calls().contains("ListEnrolledFingers"));
        QVERIFY(!fake->calls().join(',').contains("Claim"));
        QCOMPARE(FingerprintService::fingerTitle("right-index-finger"), QString("Right Index Finger"));
    }

    void addingAFingerFollowsTheReaderToTheEnd()
    {
        FingerprintService fingerprints(client, helper(), {});
        fingerprints.refresh();
        QTRY_VERIFY(!fingerprints.busy());
        fake->reset();
        QSignalSpy enrolled(&fingerprints, &FingerprintService::enrolled);
        fingerprints.enroll("right-thumb");
        QVERIFY(fingerprints.busy());
        QTRY_COMPARE(fake->calls(), QStringList({"Claim", "EnrollStart right-thumb"}));
        QTRY_COMPARE(fingerprints.message(), QString("Touch the reader with your right thumb."));
        // A second Add while adding does nothing.
        fingerprints.enroll("left-thumb");
        QVERIFY(status("enroll-stage-passed", false));
        QTRY_COMPARE(fingerprints.stage(), 1);
        QVERIFY(fingerprints.message().contains("touch the reader again"));
        QVERIFY(status("enroll-finger-not-centered", false));
        QTRY_COMPARE(fingerprints.message(), QString("Put your finger in the middle of the reader."));
        QVERIFY(status("enroll-stage-passed", false));
        QTRY_COMPARE(fingerprints.stage(), 2);
        // Another program on the bus cannot finish it.
        auto impostor = QDBusConnection::connectToBus(address, "impostor");
        auto fakeDone = QDBusMessage::createSignal(devicePath, deviceInterface, "EnrollStatus");
        fakeDone << QString("enroll-completed") << true;
        QVERIFY(impostor.send(fakeDone));
        QTest::qWait(300);
        QVERIFY(enrolled.isEmpty() && fingerprints.enrolling() == "right-thumb");
        QMetaObject::invokeMethod(fake, [this] { fake->completeEnrolment(); }, Qt::BlockingQueuedConnection);
        QVERIFY(status("enroll-completed", true));
        QTRY_COMPARE(enrolled.size(), 1);
        QCOMPARE(enrolled.first().first().toString(), QString("right-thumb"));
        QTRY_VERIFY(!fingerprints.busy());
        QCOMPARE(fingerprints.stage(), 3);
        QCOMPARE(fingerprints.message(), QString("Your right thumb is added."));
        QVERIFY(fingerprints.error().isEmpty());
        // Stopped, then given back; the list is read again, in the panel's order.
        const auto calls = fake->calls();
        QVERIFY(calls.indexOf("EnrollStop") >= 0 && calls.indexOf("EnrollStop") < calls.indexOf("Release"));
        QVERIFY(!fake->claimed());
        QTRY_COMPARE(fingerprints.fingers(), QStringList({"right-thumb", "left-index-finger"}));
        // Deleting it: claimed, deleted, given back.
        fake->reset();
        fingerprints.remove("right-thumb");
        QTRY_VERIFY(!fingerprints.busy());
        QTRY_COMPARE(fingerprints.fingers(), QStringList{"left-index-finger"});
        QCOMPARE(fake->calls().mid(0, 3), QStringList({"Claim", "DeleteEnrolledFinger right-thumb", "Release"}));
        QCOMPARE(fingerprints.message(), QString("Your right thumb is deleted."));
        QVERIFY(!fake->claimed());
    }

    void aDuplicateOrACancelGivesTheReaderBack()
    {
        FingerprintService fingerprints(client, helper(), {});
        fingerprints.refresh();
        QTRY_VERIFY(!fingerprints.busy());
        fingerprints.enroll("right-ring-finger");
        QTRY_VERIFY(fake->calls().contains("EnrollStart right-ring-finger"));
        QVERIFY(status("enroll-duplicate", true));
        QTRY_VERIFY(!fingerprints.busy());
        QCOMPARE(fingerprints.error(), QString("That fingerprint is already added."));
        QTRY_VERIFY(!fake->claimed());
        QCOMPARE(fingerprints.fingers(), QStringList{"left-index-finger"});

        fake->reset();
        fingerprints.enroll("right-ring-finger");
        QVERIFY(fingerprints.error().isEmpty());
        QTRY_VERIFY(fake->calls().contains("EnrollStart right-ring-finger"));
        fingerprints.cancel();
        QVERIFY(!fingerprints.busy() || fingerprints.enrolling().isEmpty());
        QTRY_VERIFY(!fake->claimed());
        QTRY_COMPARE(fake->calls().mid(2), QStringList({"EnrollStop", "Release"}));
        // A status arriving after Cancel changes nothing.
        QVERIFY(status("enroll-completed", true));
        QTest::qWait(200);
        QVERIFY(fingerprints.enrolling().isEmpty() && fingerprints.message().isEmpty());
    }

    void aDismissedPasswordBoxChangesNothing()
    {
        FingerprintService fingerprints(client, helper(), {});
        fingerprints.refresh();
        QTRY_VERIFY(!fingerprints.busy());
        QMetaObject::invokeMethod(fake, [this] { fake->failNextClaim("PermissionDenied"); }, Qt::BlockingQueuedConnection);
        fingerprints.enroll("right-index-finger");
        QTRY_VERIFY(!fingerprints.busy());
        QCOMPARE(fingerprints.error(), QString("The fingerprint was not added: your password was not given."));
        QVERIFY(fingerprints.enrolling().isEmpty() && !fake->claimed());
        QVERIFY(!fake->calls().join(',').contains("EnrollStart"));
        // Someone else holding the reader (the lock, another panel).
        QMetaObject::invokeMethod(fake, [this] { fake->failNextClaim("AlreadyInUse"); }, Qt::BlockingQueuedConnection);
        fingerprints.remove("left-index-finger");
        QTRY_VERIFY(!fingerprints.busy());
        QVERIFY(fingerprints.error().contains("in use"));
        QCOMPARE(fingerprints.fingers(), QStringList{"left-index-finger"});
    }

    void closingDuringAnEnrolmentReleasesTheReaderAtOnce()
    {
        {
            FingerprintService fingerprints(client, helper(), {});
            fingerprints.refresh();
            QTRY_VERIFY(!fingerprints.busy());
            fingerprints.enroll("left-thumb");
            QTRY_VERIFY(fake->claimed() && fake->calls().contains("EnrollStart left-thumb"));
        }
        QVERIFY(!fake->claimed());
    }

    void noReaderMeansNothingToAdd()
    {
        QMetaObject::invokeMethod(fake, [this] { fake->setReader(false); }, Qt::BlockingQueuedConnection);
        const auto restore = qScopeGuard([this]
                                         { QMetaObject::invokeMethod(fake, [this] { fake->setReader(true); },
                                                                     Qt::BlockingQueuedConnection); });
        FingerprintService fingerprints(client, helper(), {});
        fingerprints.refresh();
        QTRY_VERIFY(!fingerprints.busy());
        QVERIFY(fingerprints.installed() && fingerprints.reader().isEmpty() && fingerprints.fingers().isEmpty());
        fingerprints.enroll("left-thumb");
        QVERIFY(!fingerprints.busy());
    }

    void thePanelAddsAFingerAndTurnsLoginOn()
    {
        QFile::remove(directory.filePath("state.json"));
        FingerprintService fingerprints(client, helper(), {});
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *panel = view.rootObject()->findChild<QObject *>("fingerprints-panel");
        QVERIFY(panel);
        // Without a compositor the manager's own service has no bus and no helper.
        auto *own = qvariant_cast<QObject *>(panel->property("service"));
        QVERIFY(own && !own->property("installed").toBool() && !own->property("loginAvailable").toBool());
        QVERIFY(panel->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&fingerprints))));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto shown = [&](const QString &name) -> QQuickItem *
        {
            for (int attempt = 0; attempt < 150; ++attempt)
            {
                if (!view.grabWindow().isNull())
                    if (auto *item = visualItem(view.contentItem(), name); item && item->isVisible())
                        return item;
                QTest::qWait(20);
            }
            return nullptr;
        };
        const auto click = [&](const QString &name)
        {
            auto *item = shown(name);
            if (!item || !item->isEnabled())
                return false;
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        const auto label = [&](const QString &name)
        {
            auto *item = shown(name);
            return item ? item->property("text").toString() : QString();
        };
        // From the Apple menu's Control Panels, listed between Energy Saver and General Controls.
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand", Q_ARG(QVariant, "fingerprints"),
                                          Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_COMPARE(label("fingerprints-reader"), QString("Reader: Fake Reader"));
        QVERIFY(shown("fingerprint-left-index-finger"));
        QVERIFY(shown("control-panel-fingerprints"));
        QTRY_VERIFY(shown("fingerprints-login")->isEnabled());
        QVERIFY(!shown("fingerprints-delete")->isEnabled());
        QVERIFY(!shown("fingerprints-install"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/fingerprints-panel.png"));

        // Add: the first finger not yet added is offered (right index).
        fake->reset();
        QTRY_VERIFY(shown("fingerprints-add")->isEnabled());
        QVERIFY(click("fingerprints-add"));
        QTRY_VERIFY(fake->calls().contains("EnrollStart right-index-finger"));
        QTRY_COMPARE(label("fingerprints-instruction"), QString("Touch the reader with your right index finger."));
        QVERIFY(status("enroll-stage-passed", false));
        QTRY_COMPARE(fingerprints.stage(), 1);
        QVERIFY(shown("fingerprints-progress"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/fingerprints-adding.png"));
        QMetaObject::invokeMethod(fake, [this] { fake->completeEnrolment(); }, Qt::BlockingQueuedConnection);
        QVERIFY(status("enroll-completed", true));
        QTRY_VERIFY(shown("fingerprint-right-index-finger"));
        QTRY_COMPARE(label("fingerprints-message"), QString("Your right index finger is added."));
        QCOMPARE(panel->property("selected").toString(), QString("right-index-finger"));
        QVERIFY(shown("fingerprints-delete")->isEnabled());

        // Log In, lock and password boxes, through the helper (once the fresh status is read).
        QTRY_VERIFY(shown("fingerprints-login")->isEnabled());
        QVERIFY(click("fingerprints-login"));
        QTRY_VERIFY(fingerprints.login());
        QTRY_VERIFY(shown("fingerprints-login")->property("checked").toBool());
        QVERIFY(!shown("fingerprints-sudo")->property("checked").toBool());
        QVERIFY(helperCalls().contains("set on off"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/fingerprints-login-on.png"));

        // Leaving the panel mid-enrolment gives the reader back.
        QTRY_VERIFY(shown("fingerprints-add")->isEnabled());
        QVERIFY(click("fingerprints-add"));
        QTRY_VERIFY(fake->claimed());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand", Q_ARG(QVariant, "keyboard"),
                                          Q_ARG(QVariant, QVariant())));
        QTRY_VERIFY(!fake->claimed());
        QTRY_VERIFY(!fingerprints.busy());
    }

    void loginIsTurnedOnAndOffThroughTheHelperOnly()
    {
        QFile::remove(directory.filePath("helper.log"));
        QFile::remove(directory.filePath("state.json"));
        FingerprintService fingerprints(client, helper(), {});
        fingerprints.refresh();
        QTRY_VERIFY(fingerprints.loginAvailable() && !fingerprints.login());
        fingerprints.setLogin(true, false);
        QVERIFY(fingerprints.changing());
        QTRY_VERIFY(fingerprints.login());
        QTRY_VERIFY(!fingerprints.changing());
        QVERIFY(!fingerprints.sudo() && fingerprints.error().isEmpty());
        fingerprints.setLogin(true, true);
        QTRY_VERIFY(fingerprints.sudo());
        QTRY_VERIFY(!fingerprints.changing());
        fingerprints.setLogin(false, false);
        QTRY_VERIFY(!fingerprints.login() && !fingerprints.sudo());
        QTRY_VERIFY(!fingerprints.changing());
        // Each change is followed by a fresh status (the panel shows what the files now say).
        QCOMPARE(helperCalls().filter("set"), QStringList({"set on off", "set on on", "set off off"}));
        fingerprints.installSupport();
        QTRY_VERIFY(!fingerprints.changing());
        QCOMPARE(fingerprints.error(), QString("Could not install fprintd (no network)."));
        QVERIFY(helperCalls().contains("install-support"));
    }
};

QTEST_MAIN(FingerprintsTests)
#include "fingerprints_tests.moc"
