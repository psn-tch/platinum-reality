#include "appearance/interface_font.hpp"
#include "desktop/manager.hpp"
#include "desktop/system_controls.hpp"
#include "desktop/wifi.hpp"
#include "fake_iwd.hpp"
#include <QDBusPendingReply>
#include <QProcess>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// The Wi-Fi service, Control Strip module and password alert against a fake iwd on a private
// bus (docs/wifi.md). Nothing here reaches the host's system bus or Wi-Fi.
class WifiTests final : public QObject
{
    Q_OBJECT
    QProcess bus;
    QString address;
    int connections = 0;
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
    QDBusConnection connection(const QString &name)
    {
        return QDBusConnection::connectToBus(address, QString("%1-%2").arg(name).arg(++connections));
    }
    static QVariantMap find(const QVariantList &list, const QString &name)
    {
        for (const auto &entry : list)
            if (entry.toMap().value("name") == name)
                return entry.toMap();
        return {};
    }
    static QStringList names(const QVariantList &list)
    {
        QStringList result;
        for (const auto &entry : list)
            result << entry.toMap().value("name").toString();
        return result;
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
    struct Fixture
    {
        QDBusConnection iwd, client;
        FakeIwd fake;
        Fixture(QDBusConnection iwdBus, QDBusConnection clientBus)
            : iwd(std::move(iwdBus)), client(std::move(clientBus)), fake(iwd)
        {
        }
        bool start()
        {
            return iwd.registerVirtualObject("/", &fake, QDBusConnection::SubPath) && iwd.registerService("net.connman.iwd");
        }
        ~Fixture()
        {
            iwd.unregisterService("net.connman.iwd");
            iwd.unregisterObject("/");
        }
    };

  private slots:
    void initTestCase()
    {
        QGuiApplication::setFont(classicInterfaceFont());
    }
    void cleanupTestCase()
    {
        if (bus.state() != QProcess::NotRunning)
        {
            bus.terminate();
            bus.waitForFinished(3000);
        }
    }

    void networksAreListedStrongestFirstAndTheAgentRegisters()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        Fixture f(connection("iwd"), connection("client"));
        f.fake.addNetwork("Cafe", "open", -7000);
        f.fake.addNetwork("Home", "psk", -5000, true, true);
        f.fake.addNetwork("Neighbour", "psk", -8000);
        f.fake.addNetwork("Office", "8021x", -6500);
        f.fake.addNetwork("", "psk", -4000); // Hidden: no name to choose by.
        // Names come from the radio: control characters do not reach the menu.
        f.fake.addNetwork(QString("Evil\nName‮"), "open", -9000);
        QVERIFY(f.start());
        WifiService service(f.client);
        QTRY_VERIFY(service.agentRegistered());
        QVERIFY(f.fake.calls.contains("RegisterAgent"));
        QTRY_COMPARE(service.networks().size(), 5);
        QCOMPARE(names(service.networks()), (QStringList{"Home", "Office", "Cafe", "Neighbour", "Evil Name"}));
        const auto home = find(service.networks(), "Home");
        QCOMPARE(home.value("bars").toInt(), 4);
        QVERIFY(home.value("connected").toBool() && home.value("known").toBool() && home.value("secure").toBool());
        QCOMPARE(find(service.networks(), "Office").value("bars").toInt(), 3);
        QCOMPARE(find(service.networks(), "Cafe").value("bars").toInt(), 2);
        QVERIFY(!find(service.networks(), "Cafe").value("secure").toBool());
        QCOMPARE(find(service.networks(), "Neighbour").value("bars").toInt(), 1);
        QVERIFY(!find(service.networks(), "Neighbour").value("known").toBool());
        // Scanning: a network found during the scan joins the list once it settles.
        service.scan();
        QTRY_VERIFY(service.scanning());
        f.fake.addNetwork("Library", "open", -5500, false, false, true);
        f.fake.finishScan();
        QTRY_VERIFY(!service.scanning());
        QTRY_COMPARE(names(service.networks()).value(1), QString("Library"));
    }

    void joiningAsksForAPasswordThroughTheAgent()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        Fixture f(connection("iwd"), connection("client"));
        const auto cafe = f.fake.addNetwork("Cafe", "open", -6000);
        const auto neighbour = f.fake.addNetwork("Neighbour", "psk", -7000);
        f.fake.passwords[neighbour] = "correct horse";
        QVERIFY(f.start());
        WifiService service(f.client);
        QTRY_VERIFY(service.agentRegistered());
        QTRY_COMPARE(service.networks().size(), 2);

        // An open network joins at once.
        service.connectNetwork(cafe);
        QTRY_VERIFY(find(service.networks(), "Cafe").value("connected").toBool());
        QVERIFY(service.request().isEmpty());

        // A secured one: iwd asks the agent; Cancel stops joining, with nothing to say.
        service.connectNetwork(neighbour);
        QTRY_COMPARE(service.request().value("kind").toString(), QString("passphrase"));
        QCOMPARE(service.request().value("name").toString(), QString("Neighbour"));
        QCOMPARE(find(service.networks(), "Neighbour").value("busy").toString(), QString("joining"));
        service.answer(false);
        QTRY_VERIFY(f.fake.calls.contains("Canceled net.connman.iwd.Agent.Error.Canceled"));
        QTRY_COMPARE(find(service.networks(), "Neighbour").value("busy").toString(), QString());
        QVERIFY(service.message().isEmpty());

        // Too short for WPA: refused here, never sent.
        service.connectNetwork(neighbour);
        QTRY_VERIFY(!service.request().isEmpty());
        service.answer(true, "short");
        QCOMPARE(service.message(), QString("A Wi-Fi password has 8 to 63 characters."));
        QTRY_COMPARE(find(service.networks(), "Neighbour").value("busy").toString(), QString());
        QVERIFY(!f.fake.calls.contains("Passphrase short"));

        // The wrong password: iwd fails, and the message says what to check.
        service.connectNetwork(neighbour);
        QTRY_VERIFY(!service.request().isEmpty());
        service.answer(true, "wrong password");
        QTRY_COMPARE(service.message(), QString("Could not join “Neighbour”. Check the password and try again."));
        QVERIFY(!find(service.networks(), "Neighbour").value("connected").toBool());

        // The right one: joined, and iwd remembers it.
        service.connectNetwork(neighbour);
        QVERIFY(service.message().isEmpty());
        QTRY_VERIFY(!service.request().isEmpty());
        service.answer(true, "correct horse");
        QTRY_VERIFY(find(service.networks(), "Neighbour").value("connected").toBool());
        QTRY_VERIFY(find(service.networks(), "Neighbour").value("known").toBool());
        QVERIFY(!find(service.networks(), "Cafe").value("connected").toBool());

        // Leave, then forget.
        service.disconnectNetwork();
        QTRY_VERIFY(!find(service.networks(), "Neighbour").value("connected").toBool());
        const auto known = f.fake.knownPath(neighbour);
        QVERIFY(!known.isEmpty());
        service.forgetNetwork(neighbour);
        QTRY_VERIFY(f.fake.calls.contains("Forget " + known));
        QTRY_VERIFY(!find(service.networks(), "Neighbour").value("known").toBool());
    }

    void onlyIwdMayAskAndAWithdrawnQuestionCloses()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        Fixture f(connection("iwd"), connection("client"));
        const auto neighbour = f.fake.addNetwork("Neighbour", "psk", -7000);
        QVERIFY(f.start());
        WifiService service(f.client);
        QTRY_VERIFY(service.agentRegistered());
        // Another program on the bus cannot make the desktop ask for a password.
        auto impostor = connection("impostor");
        QDBusPendingReply<QString> forged =
            f.fake.ask("RequestPassphrase", {QVariant::fromValue(QDBusObjectPath(neighbour))}, impostor);
        QTRY_VERIFY(forged.isFinished());
        QCOMPARE(forged.error().type(), QDBusError::AccessDenied);
        QVERIFY(service.request().isEmpty());
        // iwd withdraws its question: the request goes away, with no reply expected.
        auto asked = f.fake.ask("RequestPassphrase", {QVariant::fromValue(QDBusObjectPath(neighbour))});
        QTRY_VERIFY(!service.request().isEmpty());
        f.fake.ask("Cancel", {QString("timed-out")});
        QTRY_VERIFY(service.request().isEmpty());
        // A user name and password, for a company network.
        auto both = f.fake.ask("RequestUserNameAndPassword", {QVariant::fromValue(QDBusObjectPath(neighbour))});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("user-password"));
        service.answer(true, "secret", "psn_tch");
        QTRY_VERIFY(both.isFinished());
        QVERIFY(!both.isError());
        QCOMPARE(both.reply().arguments(), (QVariantList{QString("psn_tch"), QString("secret")}));
    }

    void iwdStartingLaterIsFollowedAndWithoutABusNothingIsOffered()
    {
        WifiService none{QDBusConnection(QString())};
        QVERIFY(!none.available());
        QVERIFY(none.networks().isEmpty());
        none.scan();
        none.connectNetwork("/net/connman/iwd/0/3/x");
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        Fixture f(connection("iwd"), connection("client"));
        f.fake.addNetwork("Home", "psk", -5000, true, true);
        WifiService service(f.client);
        QTest::qWait(200);
        QVERIFY(!service.available());
        QVERIFY(f.start());
        QTRY_VERIFY(service.available());
        QTRY_COMPARE(service.networks().size(), 1);
        QTRY_VERIFY(service.agentRegistered());
        f.iwd.unregisterService("net.connman.iwd");
        QTRY_VERIFY(!service.available());
        QVERIFY(service.networks().isEmpty());
    }

    void stripModuleListsNetworksAndThePasswordAlertJoins()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        Fixture f(connection("iwd"), connection("client"));
        f.fake.addNetwork("Home", "psk", -5000, true, true);
        const auto neighbour = f.fake.addNetwork("Neighbour", "psk", -7000);
        f.fake.addNetwork("Cafe", "open", -7600);
        f.fake.passwords[neighbour] = "correct horse";
        QVERIFY(f.start());
        WifiService service(f.client);
        QTRY_VERIFY(service.agentRegistered());
        QTRY_COMPARE(service.networks().size(), 3);

        QTemporaryDir root;
        // Wi-Fi is on, as the controls helper reports it.
        const auto helper = root.filePath("controls.py");
        QFile script(helper);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("import json\nprint(json.dumps({'error':'','state':{'wifi':{'available':True,'enabled':True}}}))\n");
        script.close();
        SystemControls controls(helper);
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *alert = view.rootObject()->findChild<QObject *>("wifi-alert");
        QVERIFY(alert);
        // Without a compositor the manager's own service has no bus.
        QVERIFY(!qvariant_cast<QObject *>(alert->property("service"))->property("available").toBool());
        QVERIFY(alert->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&service))));
        QVERIFY(view.rootObject()->findChild<QObject *>("quick-controls")->setProperty(
            "service", QVariant::fromValue(static_cast<QObject *>(&controls))));
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
            QTest::mouseClick(&view, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        const auto text = [&](const QString &name)
        {
            auto *item = shown(name);
            return item ? item->property("modelData").toMap().value("text").toString() : QString();
        };
        const auto entry = [&](const QString &name)
        {
            auto *item = shown(name);
            return item ? item->property("modelData").toMap() : QVariantMap();
        };
        const auto label = [&](const QString &name)
        {
            auto *item = shown(name);
            return item ? item->property("text").toString() : QString();
        };
        const auto type = [&](const char *text)
        {
            for (const char *c = text; *c; ++c)
                QTest::keyClick(&view, *c);
        };

        // The module: on/off, then the networks, strongest first, with bars and locks; opening it scans.
        controls.refresh();
        QTRY_VERIFY(controls.state().contains("wifi"));
        QVERIFY(click("control-strip-wifi"));
        QTRY_VERIFY(f.fake.calls.contains("Scan"));
        f.fake.finishScan();
        QCOMPARE(text("desktop-command-status"), QString("Wi-Fi: On"));
        QCOMPARE(text("desktop-command-wifi-network-Home"), QString("Home"));
        QVERIFY(entry("desktop-command-wifi-network-Home").value("checked").toBool());
        QCOMPARE(entry("desktop-command-wifi-network-Home").value("bars").toInt(), 4);
        QVERIFY(entry("desktop-command-wifi-network-Home").value("secure").toBool());
        QVERIFY(shown("menu-signal-Home"));
        QVERIFY(!entry("desktop-command-wifi-network-Cafe").value("secure").toBool());
        QCOMPARE(text("desktop-command-wifi-disconnect"), QString("Disconnect from “Home”"));
        QCOMPARE(text("desktop-command-wifi-forget"), QString("Forget “Home”…"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/control-strip-wifi.png"));

        // Choosing a secured network asks for its password in a Platinum alert.
        QVERIFY(click("desktop-command-wifi-network-Neighbour"));
        QTRY_VERIFY(alert->property("opened").toBool());
        QCOMPARE(label("wifi-alert-text"), QString("The network “Neighbour” requires a password."));
        QTRY_VERIFY(shown("wifi-alert-password")->hasActiveFocus());
        QVERIFY(!shown("wifi-alert-accept")->isEnabled());
        type("correct horse");
        QTRY_VERIFY(shown("wifi-alert-accept")->isEnabled());
        // TextInput.Password (2), then TextInput.Normal (0).
        QCOMPARE(shown("wifi-alert-password")->property("echoMode").toInt(), 2);
        QVERIFY(click("wifi-alert-show"));
        QTRY_COMPARE(shown("wifi-alert-password")->property("echoMode").toInt(), 0);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/wifi-password-alert.png"));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(f.fake.calls.contains("Passphrase correct horse"));
        QTRY_VERIFY(!alert->property("visible").toBool());
        QTRY_VERIFY(find(service.networks(), "Neighbour").value("connected").toBool());
        QTRY_VERIFY(!manager.shellModal());

        // A wrong password is explained in an alert.
        service.disconnectNetwork();
        QTRY_VERIFY(!find(service.networks(), "Neighbour").value("connected").toBool());
        service.forgetNetwork(neighbour);
        QTRY_VERIFY(!find(service.networks(), "Neighbour").value("known").toBool());
        QVERIFY(click("control-strip-wifi"));
        QVERIFY(click("desktop-command-wifi-network-Neighbour"));
        QTRY_VERIFY(alert->property("opened").toBool());
        type("wrong password");
        QTest::keyClick(&view, Qt::Key_Return);
        auto *message = view.rootObject()->findChild<QObject *>("wifi-message-dialog");
        QTRY_VERIFY(message->property("opened").toBool());
        QCOMPARE(label("wifi-message-text"), QString("Could not join “Neighbour”. Check the password and try again."));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!message->property("visible").toBool());
        QTRY_VERIFY(service.message().isEmpty());
        QTRY_VERIFY(!manager.shellModal());

        // Forget asks first; Cancel is the default.
        QVERIFY(click("control-strip-wifi"));
        QVERIFY(click("desktop-command-wifi-network-Home"));  // Already joined: nothing happens.
        QVERIFY(!f.fake.calls.contains("Connect " + find(service.networks(), "Home").value("path").toString()));
        QVERIFY(click("control-strip-wifi"));
        QVERIFY(click("desktop-command-wifi-forget"));
        auto *forget = view.rootObject()->findChild<QObject *>("wifi-forget-dialog");
        QTRY_VERIFY(forget->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!forget->property("visible").toBool());
        QVERIFY(f.fake.calls.filter("Forget ").size() == 1);
        QVERIFY(click("control-strip-wifi"));
        QVERIFY(click("desktop-command-wifi-forget"));
        QTRY_VERIFY(forget->property("opened").toBool());
        QVERIFY(click("wifi-forget-dialog-accept"));
        QTRY_COMPARE(f.fake.calls.filter("Forget ").size(), 2);
        QTRY_VERIFY(!find(service.networks(), "Home").value("known").toBool());
    }
};

QTEST_MAIN(WifiTests)
#include "wifi_tests.moc"
