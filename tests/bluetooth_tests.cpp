#include "appearance/interface_font.hpp"
#include "desktop/bluetooth.hpp"
#include "desktop/manager.hpp"
#include "desktop/system_controls.hpp"
#include "fake_bluez.hpp"
#include <QDBusPendingReply>
#include <QProcess>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// The Bluetooth service, panel, Control Strip module and pairing alerts against a fake BlueZ on
// a private bus (docs/bluetooth.md). Nothing here reaches the host's system bus or adapter.
class BluetoothTests final : public QObject
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
    }
    void cleanupTestCase()
    {
        if (bus.state() != QProcess::NotRunning)
        {
            bus.terminate();
            bus.waitForFinished(3000);
        }
    }

    void devicesPairedElsewhereAreListedAndTheAgentIsTheDefault()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto bluez = connection("bluez"), client = connection("client");
        FakeBluez fake(bluez);
        const auto headphones = fake.addDevice("AA:00:00:00:00:01", "Test Headphones", "audio-headset", true, true, true, 80);
        fake.addDevice("AA:00:00:00:00:02", "Test Mouse", "input-mouse", true, false, false);
        fake.addDevice("AA:00:00:00:00:03", "Test Speaker", "audio-card", false, false, false);
        fake.addDevice("AA:00:00:00:00:04", "", "", false, false, false);
        QVERIFY(bluez.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
        QVERIFY(bluez.registerService("org.bluez"));
        const auto cleanup = qScopeGuard([&] { bluez.unregisterService("org.bluez"); bluez.unregisterObject("/"); });

        BluetoothService service(client);
        QTRY_VERIFY(service.available());
        QTRY_VERIFY(service.agentRegistered());
        QTRY_VERIFY(fake.defaultAgent);
        QCOMPARE(fake.agentOwner, client.baseService());
        QVERIFY(fake.calls.contains("RegisterAgent KeyboardDisplay"));
        QVERIFY(service.powered());
        {
            // A desktop in a window inside another desktop registers, but leaves the default.
            auto nested = connection("nested");
            fake.defaultAgent = false;
            fake.calls.clear();
            BluetoothService inside(nested, false);
            QTRY_VERIFY(inside.agentRegistered());
            QTRY_COMPARE(inside.devices().size(), 2);
            QCOMPARE(fake.calls.filter("Agent"), QStringList({"RegisterAgent KeyboardDisplay"}));
            QVERIFY(!fake.defaultAgent);
            QCOMPARE(fake.agentOwner, nested.baseService());
        }
        QCOMPARE(service.computerName(), QString("test-computer"));
        // Paired devices, by name, with what the panel shows; found devices only when named.
        QTRY_COMPARE(service.devices().size(), 2);
        const auto first = service.devices().first().toMap();
        QCOMPARE(first.value("path").toString(), headphones);
        QCOMPARE(first.value("name").toString(), QString("Test Headphones"));
        QCOMPARE(first.value("kind").toString(), QString("headphones"));
        QCOMPARE(first.value("type").toString(), QString("Headphones"));
        QVERIFY(first.value("connected").toBool() && first.value("trusted").toBool());
        QCOMPARE(first.value("battery").toInt(), 80);
        const auto mouse = find(service.devices(), "Test Mouse");
        QCOMPARE(mouse.value("kind").toString(), QString("mouse"));
        QCOMPARE(mouse.value("battery").toInt(), -1);
        QVERIFY(!mouse.value("connected").toBool() && !mouse.value("trusted").toBool());
        QCOMPARE(service.nearby().size(), 1);
        QCOMPARE(service.nearby().first().toMap().value("name").toString(), QString("Test Speaker"));

        // Live changes: battery and connection.
        fake.change(headphones, "org.bluez.Battery1", "Percentage", QVariant::fromValue(uchar(55)));
        QTRY_COMPARE(find(service.devices(), "Test Headphones").value("battery").toInt(), 55);
        fake.change(headphones, FakeBluez::device, "Connected", false);
        QTRY_VERIFY(!find(service.devices(), "Test Headphones").value("connected").toBool());
    }

    void connectDisconnectForgetAndPower()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto bluez = connection("bluez"), client = connection("client");
        FakeBluez fake(bluez);
        const auto mouse = fake.addDevice("AA:00:00:00:00:02", "Test Mouse", "input-mouse", true, true, false);
        const auto pad = fake.addDevice("AA:00:00:00:00:05", "Test Pad", "input-gaming", true, true, false);
        QVERIFY(bluez.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
        QVERIFY(bluez.registerService("org.bluez"));
        const auto cleanup = qScopeGuard([&] { bluez.unregisterService("org.bluez"); bluez.unregisterObject("/"); });
        BluetoothService service(client);
        QTRY_COMPARE(service.devices().size(), 2);

        service.connectDevice(mouse);
        QCOMPARE(find(service.devices(), "Test Mouse").value("busy").toString(), QString("connecting"));
        QTRY_VERIFY(find(service.devices(), "Test Mouse").value("connected").toBool());
        QTRY_COMPARE(find(service.devices(), "Test Mouse").value("busy").toString(), QString());
        QVERIFY(fake.calls.contains("Connect " + mouse));
        // A device that is off or out of range: said plainly.
        fake.failConnect.insert(pad);
        service.connectDevice(pad);
        QTRY_COMPARE(service.message(), QString("“Test Pad” could not connect. Make sure it is turned on and nearby."));
        QVERIFY(!find(service.devices(), "Test Pad").value("connected").toBool());
        service.disconnectDevice(mouse);
        QTRY_VERIFY(!find(service.devices(), "Test Mouse").value("connected").toBool());
        QVERIFY(service.message().isEmpty());
        service.forgetDevice(pad);
        QTRY_COMPARE(service.devices().size(), 1);
        QVERIFY(fake.calls.contains("RemoveDevice " + pad));
        service.setPowered(false);
        QTRY_VERIFY(!service.powered());
        QVERIFY(fake.calls.contains(QString("Set %1 Powered false").arg(FakeBluez::adapter)));
    }

    void pairingAsksThroughTheAgentThenTrustsAndConnects()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto bluez = connection("bluez"), client = connection("client"), impostor = connection("impostor");
        FakeBluez fake(bluez);
        QVERIFY(bluez.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
        QVERIFY(bluez.registerService("org.bluez"));
        const auto cleanup = qScopeGuard([&] { bluez.unregisterService("org.bluez"); bluez.unregisterObject("/"); });
        BluetoothService service(client);
        QSignalSpy paired(&service, &BluetoothService::paired);
        QTRY_VERIFY(service.agentRegistered());

        service.startDiscovery();
        QTRY_VERIFY(service.discovering());
        const auto speaker = fake.addDevice("AA:00:00:00:00:03", "Test Speaker", "audio-card", false, false, false, -1, true);
        QTRY_COMPARE(service.nearby().size(), 1);
        service.pairDevice(speaker);
        QTRY_VERIFY(fake.pendingPair.has_value());
        // Discovery stops for pairing.
        QVERIFY(fake.calls.contains("StopDiscovery"));

        // Only BlueZ may ask the agent.
        auto forged = fake.ask("RequestConfirmation", {QVariant::fromValue(QDBusObjectPath(speaker)), 111111u}, impostor);
        QTRY_VERIFY(forged.isFinished());
        QCOMPARE(forged.error().type(), QDBusError::AccessDenied);
        QVERIFY(service.request().isEmpty());

        auto confirm = fake.ask("RequestConfirmation", {QVariant::fromValue(QDBusObjectPath(speaker)), 42u});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("confirm"));
        QCOMPARE(service.request().value("code").toString(), QString("000042"));
        QCOMPARE(service.request().value("name").toString(), QString("Test Speaker"));
        // A second question while one is on screen is declined.
        auto overlapping = fake.ask("RequestAuthorization", {QVariant::fromValue(QDBusObjectPath(speaker))});
        QTRY_VERIFY(overlapping.isFinished());
        QCOMPARE(overlapping.error().name(), QString("org.bluez.Error.Rejected"));
        QVERIFY(!confirm.isFinished());
        service.answer(true);
        QTRY_VERIFY(confirm.isFinished());
        QVERIFY(!confirm.isError());
        QVERIFY(service.request().isEmpty());
        fake.finishPair(true);
        QTRY_COMPARE(paired.size(), 1);
        QCOMPARE(paired.first().first().toString(), speaker);
        QTRY_VERIFY(fake.calls.contains(QString("Set %1 Trusted true").arg(speaker)));
        QTRY_VERIFY(fake.calls.contains("Connect " + speaker));
        QTRY_VERIFY(find(service.devices(), "Test Speaker").value("connected").toBool());
        QVERIFY(service.nearby().isEmpty());

        // A failed pairing says why, and leaves the device unpaired.
        const auto keyboard = fake.addDevice("AA:00:00:00:00:06", "Test Keyboard", "input-keyboard", false, false, false, -1, true);
        QTRY_COMPARE(service.nearby().size(), 1);
        service.pairDevice(keyboard);
        QTRY_VERIFY(fake.pendingPair.has_value());
        auto shown = fake.ask("DisplayPasskey", {QVariant::fromValue(QDBusObjectPath(keyboard)), 9u, QVariant::fromValue(ushort(0))});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("display-passkey"));
        QTRY_VERIFY(shown.isFinished());
        QVERIFY(!shown.isError());
        const auto id = service.request().value("id");
        fake.ask("DisplayPasskey", {QVariant::fromValue(QDBusObjectPath(keyboard)), 9u, QVariant::fromValue(ushort(3))});
        QTRY_COMPARE(service.request().value("entered").toInt(), 3);
        QCOMPARE(service.request().value("id"), id);
        QCOMPARE(service.request().value("code").toString(), QString("000009"));
        // Cancel on the shown code cancels the pairing.
        service.answer(false);
        QTRY_VERIFY(fake.calls.contains("CancelPairing " + keyboard));
        QTRY_COMPARE(service.message(), QString("Pairing with “Test Keyboard” was canceled."));
        QCOMPARE(paired.size(), 1);
        QVERIFY(service.request().isEmpty());
        QVERIFY(!fake.value(keyboard, FakeBluez::device, "Paired").toBool());
    }

    void pinPasskeyAndPermissionAnswers()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto bluez = connection("bluez"), client = connection("client");
        FakeBluez fake(bluez);
        const auto mouse = fake.addDevice("AA:00:00:00:00:02", "Test Mouse", "input-mouse", true, false, false);
        QVERIFY(bluez.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
        QVERIFY(bluez.registerService("org.bluez"));
        const auto cleanup = qScopeGuard([&] { bluez.unregisterService("org.bluez"); bluez.unregisterObject("/"); });
        BluetoothService service(client);
        QTRY_VERIFY(service.agentRegistered());
        const auto device = QVariant::fromValue(QDBusObjectPath(mouse));

        QDBusPendingReply<QString> pin = fake.ask("RequestPinCode", {device});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("pin"));
        service.answer(true, " 0000 ");
        QTRY_VERIFY(pin.isFinished());
        QCOMPARE(pin.value(), QString("0000"));

        QDBusPendingReply<uint> passkey = fake.ask("RequestPasskey", {device});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("passkey"));
        service.answer(true, "123456");
        QTRY_VERIFY(passkey.isFinished());
        QCOMPARE(passkey.value(), 123456u);

        // Not a passkey: declined, and said why.
        QDBusPendingReply<uint> wrong = fake.ask("RequestPasskey", {device});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("passkey"));
        service.answer(true, "1234567");
        QTRY_VERIFY(wrong.isFinished());
        QCOMPARE(wrong.error().name(), QString("org.bluez.Error.Rejected"));
        QCOMPARE(service.message(), QString("A passkey is a number of up to six digits."));

        // An untrusted device asking to connect: once, then always.
        auto once = fake.ask("AuthorizeService", {device, QString("00001124-0000-1000-8000-00805f9b34fb")});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("authorize-service"));
        service.answer(true);
        QTRY_VERIFY(once.isFinished());
        QVERIFY(!once.isError());
        QVERIFY(!fake.value(mouse, FakeBluez::device, "Trusted").toBool());
        auto declined = fake.ask("AuthorizeService", {device, QString("00001124-0000-1000-8000-00805f9b34fb")});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("authorize-service"));
        service.answer(false);
        QTRY_VERIFY(declined.isFinished());
        QCOMPARE(declined.error().name(), QString("org.bluez.Error.Rejected"));
        auto always = fake.ask("AuthorizeService", {device, QString("00001124-0000-1000-8000-00805f9b34fb")});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("authorize-service"));
        service.answer(true, {}, true);
        QTRY_VERIFY(always.isFinished());
        QVERIFY(!always.isError());
        QTRY_VERIFY(fake.value(mouse, FakeBluez::device, "Trusted").toBool());
        QTRY_VERIFY(find(service.devices(), "Test Mouse").value("trusted").toBool());

        // BlueZ withdrawing a question (it timed out) closes it without an answer.
        auto withdrawn = fake.ask("RequestAuthorization", {device});
        QTRY_COMPARE(service.request().value("kind").toString(), QString("authorize-pairing"));
        auto cancel = fake.ask("Cancel", {});
        QTRY_VERIFY(service.request().isEmpty());
        QTRY_VERIFY(cancel.isFinished());
        QTest::qWait(200);
        QVERIFY(!withdrawn.isFinished());
    }

    void blueZStartingLaterAndRestartingIsFollowed()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto bluez = connection("bluez"), client = connection("client");
        FakeBluez fake(bluez);
        fake.addDevice("AA:00:00:00:00:02", "Test Mouse", "input-mouse", true, true, false);
        QVERIFY(bluez.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
        const auto cleanup = qScopeGuard([&] { bluez.unregisterService("org.bluez"); bluez.unregisterObject("/"); });
        BluetoothService service(client);
        QTest::qWait(300);
        QVERIFY(!service.available() && service.devices().isEmpty() && !service.agentRegistered());
        QVERIFY(bluez.registerService("org.bluez"));
        QTRY_VERIFY(service.available());
        QTRY_VERIFY(service.agentRegistered());
        QTRY_COMPARE(service.devices().size(), 1);
        // BlueZ stops: nothing is offered; it returns: the agent registers again.
        QVERIFY(bluez.unregisterService("org.bluez"));
        QTRY_VERIFY(!service.available());
        QVERIFY(service.devices().isEmpty() && !service.agentRegistered());
        fake.calls.clear();
        QVERIFY(bluez.registerService("org.bluez"));
        QTRY_VERIFY(service.agentRegistered());
        QVERIFY(fake.calls.contains("RegisterAgent KeyboardDisplay"));
        QTRY_COMPARE(service.devices().size(), 1);
    }

    void withoutASystemBusNothingIsOffered()
    {
        BluetoothService service(QDBusConnection(QString{}));
        QTest::qWait(100);
        QVERIFY(!service.available() && !service.powered() && !service.agentRegistered());
        QVERIFY(service.devices().isEmpty() && service.nearby().isEmpty() && service.request().isEmpty());
        // Requests are ignored rather than failing loudly.
        service.connectDevice("/org/bluez/hci0/dev_AA_00_00_00_00_02");
        service.startDiscovery();
        service.setPowered(true);
        QVERIFY(service.message().isEmpty());
    }

    void panelStripModuleAndPairingAlert()
    {
        if (!busReady())
            QSKIP("dbus-daemon is not available");
        auto bluez = connection("bluez"), client = connection("client");
        FakeBluez fake(bluez);
        const auto headphones = fake.addDevice("AA:00:00:00:00:01", "Test Headphones", "audio-headset", true, true, true, 80);
        const auto mouse = fake.addDevice("AA:00:00:00:00:02", "Test Mouse", "input-mouse", true, false, false);
        fake.addDevice("AA:00:00:00:00:05", "Test Pad", "input-gaming", true, true, false);
        QVERIFY(bluez.registerVirtualObject("/", &fake, QDBusConnection::SubPath));
        QVERIFY(bluez.registerService("org.bluez"));
        const auto cleanup = qScopeGuard([&] { bluez.unregisterService("org.bluez"); bluez.unregisterObject("/"); });
        BluetoothService service(client);
        QTRY_VERIFY(service.agentRegistered());
        QTRY_COMPARE(service.devices().size(), 3);

        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *panel = view.rootObject()->findChild<QObject *>("bluetooth-panel");
        QVERIFY(panel);
        // Without a compositor the manager's own service has no bus.
        QVERIFY(!qvariant_cast<QObject *>(panel->property("service"))->property("available").toBool());
        QVERIFY(panel->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&service))));
        // Quick Controls' other settings come from a helper that is absent here: nothing is read.
        SystemControls noHelper(root.filePath("absent-controls.py"));
        QVERIFY(view.rootObject()->findChild<QObject *>("quick-controls")->setProperty(
            "service", QVariant::fromValue(static_cast<QObject *>(&noHelper))));
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
        const auto click = [&](const QString &name, bool twice = false)
        {
            auto *item = shown(name);
            if (!item || !item->isEnabled())
                return false;
            const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            if (twice)
                QTest::mouseDClick(&view, Qt::LeftButton, {}, point);
            else
                QTest::mouseClick(&view, Qt::LeftButton, {}, point);
            return true;
        };
        const auto text = [&](const QString &name)
        {
            auto *item = shown(name);
            return item ? item->property("modelData").toMap().value("text").toString() : QString();
        };
        const auto label = [&](const QString &name)
        {
            auto *item = shown(name);
            return item ? item->property("text").toString() : QString();
        };

        // The strip module: power, then the paired devices; connected ones checked, with battery.
        QVERIFY(click("control-strip-bluetooth"));
        QCOMPARE(text("desktop-command-bluetooth"), QString("Open Bluetooth…"));
        QCOMPARE(text("desktop-command-status"), QString("Bluetooth: On"));
        QCOMPARE(text("desktop-command-bluetooth-device-Test Headphones"), QString("Test Headphones — 80%"));
        QVERIFY(shown("desktop-command-bluetooth-device-Test Headphones")->property("modelData").toMap().value("checked").toBool());
        QCOMPARE(text("desktop-command-bluetooth-device-Test Mouse"), QString("Test Mouse"));
        QVERIFY(!shown("desktop-command-bluetooth-device-Test Mouse")->property("modelData").toMap().value("checked").toBool());
        QVERIFY(shown("desktop-command-bluetooth-add"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/control-strip-bluetooth.png"));
        QVERIFY(click("desktop-command-bluetooth-device-Test Mouse"));
        QTRY_VERIFY(fake.calls.contains("Connect " + mouse));
        QTRY_VERIFY(find(service.devices(), "Test Mouse").value("connected").toBool());
        QTRY_VERIFY(!visualItem(view.contentItem(), "control-strip-menu")->isVisible());

        // The control panel, from the module's first item.
        QVERIFY(click("control-strip-bluetooth"));
        QVERIFY(click("desktop-command-bluetooth"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(manager.shellModal());
        QCOMPARE(label("bluetooth-power-state"), QString("Bluetooth: On"));
        QCOMPARE(label("bluetooth-status-Test Headphones"), QString("Headphones — Connected, battery 80%"));
        QTRY_COMPARE(label("bluetooth-status-Test Mouse"), QString("Mouse — Connected"));
        QCOMPARE(label("bluetooth-status-Test Pad"), QString("Game Controller — Not connected"));
        QVERIFY(!shown("bluetooth-connect")->isEnabled());
        QVERIFY(click("bluetooth-device-Test Mouse"));
        QTRY_COMPARE(label("bluetooth-connect"), QString("Disconnect"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/bluetooth-panel.png"));
        QVERIFY(click("bluetooth-connect"));
        QTRY_VERIFY(fake.calls.contains("Disconnect " + mouse));
        QTRY_COMPARE(label("bluetooth-status-Test Mouse"), QString("Mouse — Not connected"));
        QTRY_COMPARE(label("bluetooth-connect"), QString("Connect"));
        // A double click connects.
        QVERIFY(click("bluetooth-device-Test Pad", true));
        QTRY_COMPARE(label("bluetooth-status-Test Pad"), QString("Game Controller — Connected"));

        // Forget asks first; Cancel is the default.
        QVERIFY(click("bluetooth-device-Test Mouse"));
        QVERIFY(click("bluetooth-forget"));
        auto *forget = view.rootObject()->findChild<QObject *>("bluetooth-forget-dialog");
        QTRY_VERIFY(forget->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!forget->property("visible").toBool());
        QVERIFY(!fake.calls.contains("RemoveDevice " + mouse));
        QVERIFY(click("bluetooth-forget"));
        QTRY_VERIFY(forget->property("opened").toBool());
        QVERIFY(click("bluetooth-forget-dialog-accept"));
        QTRY_VERIFY(fake.calls.contains("RemoveDevice " + mouse));
        // The confirmation takes the pointer until it has finished closing.
        QTRY_VERIFY(!forget->property("visible").toBool());
        QTRY_VERIFY(!visualItem(view.contentItem(), "bluetooth-device-Test Mouse"));
        QTRY_COMPARE(service.devices().size(), 2);

        // Add Device: discovery, the found device, Pair, then the confirmation alert.
        QVERIFY(click("bluetooth-add"));
        QTRY_VERIFY(fake.calls.contains("StartDiscovery"));
        QCOMPARE(panel->property("title").toString(), QString("Add Bluetooth Device"));
        const auto speaker = fake.addDevice("AA:00:00:00:00:03", "Test Speaker", "audio-card", false, false, false, -1, true);
        fake.addDevice("AA:00:00:00:00:04", "", "", false, false, false, -1, true);
        QVERIFY(!shown("bluetooth-pair")->isEnabled());
        QVERIFY(click("bluetooth-device-Test Speaker"));
        QVERIFY(click("bluetooth-pair"));
        QTRY_VERIFY(fake.pendingPair.has_value());
        QTRY_COMPARE(label("bluetooth-message"), QString("Pairing with “Test Speaker”…"));
        auto confirm = fake.ask("RequestConfirmation", {QVariant::fromValue(QDBusObjectPath(speaker)), 123456u});
        auto *alert = view.rootObject()->findChild<QObject *>("bluetooth-alert");
        QTRY_VERIFY(alert->property("opened").toBool());
        QCOMPARE(label("bluetooth-alert-code"), QString("123456"));
        QCOMPARE(label("bluetooth-alert-text"), QString("Does “Test Speaker” show this code? Pair only if the codes match."));
        QCOMPARE(label("bluetooth-alert-accept"), QString("Pair"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/bluetooth-pairing-alert.png"));
        QVERIFY(click("bluetooth-alert-accept"));
        QTRY_VERIFY(confirm.isFinished());
        QVERIFY(!confirm.isError());
        QTRY_VERIFY(!alert->property("visible").toBool());
        fake.finishPair(true);
        QTRY_VERIFY(fake.calls.contains("Connect " + speaker));
        QTRY_VERIFY(!panel->property("adding").toBool());
        QCOMPARE(panel->property("selected").toString(), speaker);
        QTRY_COMPARE(label("bluetooth-status-Test Speaker"), QString("Audio — Connected"));
        QVERIFY(fake.calls.contains("StopDiscovery"));

        // An untrusted device asking to connect while the panel is closed: Always Allow trusts it.
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
        QTRY_VERIFY(!manager.shellModal());
        const auto keyboard = fake.addDevice("AA:00:00:00:00:06", "Test Keyboard", "input-keyboard", true, false, false, -1, true);
        auto authorize = fake.ask("AuthorizeService", {QVariant::fromValue(QDBusObjectPath(keyboard)),
                                                       QString("00001124-0000-1000-8000-00805f9b34fb")});
        QTRY_VERIFY(alert->property("opened").toBool());
        QCOMPARE(label("bluetooth-alert-text"), QString("“Test Keyboard” wants to connect to this computer."));
        QCOMPARE(label("bluetooth-alert-cancel"), QString("Don’t Allow"));
        QVERIFY(click("bluetooth-alert-always"));
        QVERIFY(click("bluetooth-alert-accept"));
        QTRY_VERIFY(authorize.isFinished());
        QVERIFY(!authorize.isError());
        QTRY_VERIFY(fake.value(keyboard, FakeBluez::device, "Trusted").toBool());
        QTRY_VERIFY(!manager.shellModal());

        // A PIN: typed, then Return. Escape declines the next question.
        QDBusPendingReply<QString> pin = fake.ask("RequestPinCode", {QVariant::fromValue(QDBusObjectPath(keyboard))});
        QTRY_VERIFY(alert->property("opened").toBool());
        QTRY_VERIFY(shown("bluetooth-alert-field")->hasActiveFocus());
        QVERIFY(!shown("bluetooth-alert-accept")->isEnabled());
        for (int digit = 0; digit < 4; ++digit)
            QTest::keyClick(&view, Qt::Key_0);
        QTRY_VERIFY(shown("bluetooth-alert-accept")->isEnabled());
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(pin.isFinished());
        QCOMPARE(pin.value(), QString("0000"));
        auto escaped = fake.ask("RequestAuthorization", {QVariant::fromValue(QDBusObjectPath(keyboard))});
        QTRY_VERIFY(alert->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(escaped.isFinished());
        QCOMPARE(escaped.error().name(), QString("org.bluez.Error.Rejected"));
        QTRY_VERIFY(!manager.shellModal());

        // The Control Panels menu and Quick Controls offer Bluetooth too.
        auto *quick = view.rootObject()->findChild<QObject *>("quick-controls");
        QCOMPARE(qvariant_cast<QObject *>(quick->property("bluetooth")), static_cast<QObject *>(&service));
        QVERIFY(QMetaObject::invokeMethod(quick, "showControls"));
        QTRY_VERIFY(quick->property("opened").toBool());
        QCOMPARE(label("quick-bluetooth"), QString("Turn Off"));
        QVERIFY(click("quick-bluetooth"));
        QTRY_VERIFY(!service.powered());
        QTRY_COMPARE(label("quick-bluetooth"), QString("Turn On"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(click("control-strip-bluetooth"));
        QCOMPARE(text("desktop-command-bluetooth-power"), QString("Turn Bluetooth On"));
        QCOMPARE(text("desktop-command-status"), QString("Bluetooth: Off"));
        QVERIFY(!visualItem(view.contentItem(), "desktop-command-bluetooth-device-Test Headphones"));
        QVERIFY(click("desktop-command-bluetooth-power"));
        QTRY_VERIFY(service.powered());
        QVERIFY(fake.calls.contains(QString("Set %1 Powered true").arg(FakeBluez::adapter)));
        QVERIFY(fake.value(headphones, FakeBluez::device, "Connected").toBool());
    }
};

QTEST_MAIN(BluetoothTests)
#include "bluetooth_tests.moc"
