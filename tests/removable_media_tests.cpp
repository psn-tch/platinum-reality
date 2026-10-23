#include "desktop/removable_media.hpp"
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

// UDisks2's D-Bus surface, enough for drives, block devices and filesystems: the object manager,
// property changes, Filesystem.Mount/Unmount and Drive.PowerOff. It records every call and
// answers from the test's event loop. Nothing here reaches the host's system bus or disks.
class FakeUDisks final : public QDBusVirtualObject
{
  public:
    static constexpr auto root = "/org/freedesktop/UDisks2";
    QStringList calls;
    QString busyError;
    QMap<QString, BluetoothInterfaces> objects;

    explicit FakeUDisks(QDBusConnection bus) : m_bus(std::move(bus)) {}
    // A drive and one partition with a filesystem; returns the partition's path.
    QString add(const QString &name, bool removable, bool hintSystem, bool hintIgnore, bool announce)
    {
        const auto drive = QString(root) + "/drives/" + name;
        const auto block = QString(root) + "/block_devices/" + name + "1";
        objects[drive] = {{"org.freedesktop.UDisks2.Drive",
                           {{"Removable", removable}, {"MediaRemovable", false}, {"CanPowerOff", removable},
                            {"Ejectable", false}}}};
        objects[block] = {{"org.freedesktop.UDisks2.Block",
                           {{"Drive", QVariant::fromValue(QDBusObjectPath(drive))}, {"HintAuto", removable},
                            {"HintSystem", hintSystem}, {"HintIgnore", hintIgnore}, {"IdUsage", "filesystem"},
                            {"IdLabel", name.toUpper()}}},
                          {"org.freedesktop.UDisks2.Filesystem",
                           {{"MountPoints", QVariant::fromValue(QList<QByteArray>())}}}};
        if (announce)
            for (const auto &path : {drive, block})
            {
                auto signal = QDBusMessage::createSignal(root, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");
                signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant::fromValue(objects[path]);
                m_bus.send(signal);
            }
        return block;
    }
    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const auto path = message.path();
        calls << message.member() + " " + path.section('/', -1);
        if (message.member() == "GetManagedObjects")
        {
            BluetoothObjects all;
            for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                all[QDBusObjectPath(it.key())] = it.value();
            return connection.send(message.createReply(QVariant::fromValue(all)));
        }
        if (message.member() == "Mount" || message.member() == "Unmount")
        {
            if (message.member() == "Unmount" && !busyError.isEmpty())
                return connection.send(message.createErrorReply("org.freedesktop.UDisks2.Error.DeviceBusy", busyError));
            const auto label = objects[path]["org.freedesktop.UDisks2.Block"].value("IdLabel").toString();
            const auto mount = "/run/media/someone/" + label;
            const QList<QByteArray> points = message.member() == "Mount" ? QList<QByteArray>{mount.toUtf8() + '\0'}
                                                                           : QList<QByteArray>{};
            objects[path]["org.freedesktop.UDisks2.Filesystem"]["MountPoints"] = QVariant::fromValue(points);
            auto changed = QDBusMessage::createSignal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
            changed << "org.freedesktop.UDisks2.Filesystem"
                    << QVariantMap{{"MountPoints", QVariant::fromValue(points)}} << QStringList();
            m_bus.send(changed);
            return connection.send(message.member() == "Mount" ? message.createReply(mount) : message.createReply());
        }
        return connection.send(message.createReply());
    }

  private:
    QDBusConnection m_bus;
};

class RemovableMediaTests final : public QObject
{
    Q_OBJECT
    QProcess bus;
    QString address;
    int connections = 0;
    QDBusConnection connection(const QString &name)
    {
        return QDBusConnection::connectToBus(address, QString("%1-%2").arg(name).arg(++connections));
    }

  private slots:
    void initTestCase()
    {
        qDBusRegisterMetaType<QList<QByteArray>>();
        qDBusRegisterMetaType<BluetoothInterfaces>();
        qDBusRegisterMetaType<BluetoothObjects>();
        bus.start("dbus-daemon", {"--session", "--nofork", "--print-address"});
        QVERIFY(bus.waitForStarted() && bus.waitForReadyRead(5000));
        address = QString::fromUtf8(bus.readLine()).trimmed();
    }
    void cleanupTestCase()
    {
        bus.terminate();
        bus.waitForFinished(3000);
    }
    void onlyRemovableDisksAreMountedAndEjectPutsThemAway()
    {
        auto service = connection("udisks");
        FakeUDisks udisks(service);
        QVERIFY(service.registerVirtualObject(FakeUDisks::root, &udisks, QDBusConnection::SubPath));
        QVERIFY(service.registerService("org.freedesktop.UDisks2"));
        // At login: a stick left in, the internal disk, and a removable partition marked to ignore.
        const auto stick = udisks.add("stick", true, false, false, false);
        udisks.add("nvme", false, true, false, false);
        udisks.add("hidden", true, false, true, false);
        RemovableMedia media(connection("desktop"), true);
        QSignalSpy mounted(&media, &RemovableMedia::mounted);
        QTRY_COMPARE(mounted.count(), 1);
        QCOMPARE(mounted.first().first().toString(), QString("/run/media/someone/STICK"));
        QCOMPARE(udisks.calls.filter("Mount ").join(','), QString("Mount stick1"));
        QTRY_VERIFY(media.canEject("/run/media/someone/STICK"));
        QVERIFY(!media.canEject("/run/media/someone/NVME"));
        // Plugged in later: mounted once.
        udisks.add("card", true, false, false, true);
        QTRY_COMPARE(mounted.count(), 2);
        QCOMPARE(mounted.last().first().toString(), QString("/run/media/someone/CARD"));
        // In use: it stays, and says why.
        QSignalSpy failed(&media, &RemovableMedia::failed);
        udisks.busyError = "target is busy";
        QVERIFY(media.eject("/run/media/someone/STICK"));
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(),
                 QString("The disk “STICK” could not be put away, because it contains items that are in use."));
        // Eject: unmounted, then the drive is powered off; it is not mounted again by itself.
        udisks.busyError.clear();
        QSignalSpy ejected(&media, &RemovableMedia::ejected);
        QVERIFY(media.eject("/run/media/someone/STICK"));
        QTRY_COMPARE(ejected.count(), 1);
        QTRY_VERIFY(udisks.calls.contains("PowerOff stick"));
        QTRY_VERIFY(!media.canEject("/run/media/someone/STICK"));
        QTest::qWait(200);
        QCOMPARE(udisks.calls.filter("Mount stick1").size(), 1);
        QVERIFY(!media.eject("/run/media/someone/NOT-A-DISK"));
        Q_UNUSED(stick);
        service.unregisterService("org.freedesktop.UDisks2");
    }
    void withoutAutomountNothingIsMounted()
    {
        auto service = connection("udisks");
        FakeUDisks udisks(service);
        QVERIFY(service.registerVirtualObject(FakeUDisks::root, &udisks, QDBusConnection::SubPath));
        QVERIFY(service.registerService("org.freedesktop.UDisks2"));
        udisks.add("stick", true, false, false, false);
        // A desktop in a window: the desktop around it mounts disks.
        RemovableMedia media(connection("desktop"), false);
        QTRY_VERIFY(udisks.calls.contains("GetManagedObjects UDisks2"));
        QTest::qWait(200);
        QVERIFY(udisks.calls.filter("Mount").isEmpty());
        service.unregisterService("org.freedesktop.UDisks2");
        // And without a bus, nothing at all.
        RemovableMedia none{QDBusConnection(QString()), true};
        QVERIFY(!none.available() && !none.canEject("/run/media/someone/STICK"));
    }
};

QTEST_GUILESS_MAIN(RemovableMediaTests)
#include "removable_media_tests.moc"
