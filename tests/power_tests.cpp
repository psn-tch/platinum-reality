#include "desktop/power_control.hpp"
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

// logind's Manager methods that Special ▸ Sleep, Restart and Shut Down use, on a private bus:
// records each call and its interactive flag, and refuses when asked to.
class FakeLogind final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")
  public:
    QStringList calls;
    QString refusal;

  public slots:
    void Suspend(bool interactive) { answer("Suspend", interactive); }
    void Reboot(bool interactive) { answer("Reboot", interactive); }
    void PowerOff(bool interactive) { answer("PowerOff", interactive); }

  private:
    void answer(const QString &method, bool interactive)
    {
        calls << method + (interactive ? " interactive" : "");
        if (!refusal.isEmpty())
            sendErrorReply("org.freedesktop.login1.OperationInProgress", refusal);
    }
};

class PowerTests : public QObject
{
    Q_OBJECT
    QProcess bus;
    QString address;

  private slots:
    void initTestCase()
    {
        bus.start("dbus-daemon", {"--session", "--nofork", "--print-address"});
        QVERIFY(bus.waitForStarted() && bus.waitForReadyRead(5000));
        address = QString::fromUtf8(bus.readLine()).trimmed();
        QVERIFY(!address.isEmpty());
    }
    void cleanupTestCase()
    {
        bus.terminate();
        bus.waitForFinished(3000);
    }
    void eachCommandAsksLogindInteractively()
    {
        auto service = QDBusConnection::connectToBus(address, "logind");
        FakeLogind logind;
        QVERIFY(service.registerObject("/org/freedesktop/login1", &logind, QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService("org.freedesktop.login1"));
        PowerControl power(QDBusConnection::connectToBus(address, "desktop"));
        QVERIFY(power.available());
        QSignalSpy finished(&power, &PowerControl::finished);
        for (const auto action : {PowerControl::Action::Sleep, PowerControl::Action::Restart,
                                  PowerControl::Action::ShutDown})
        {
            QVERIFY(power.request(action));
            QVERIFY(power.busy());
            // One request at a time.
            QVERIFY(!power.request(PowerControl::Action::Sleep));
            QTRY_VERIFY(!power.busy());
        }
        QCOMPARE(logind.calls, QStringList({"Suspend interactive", "Reboot interactive", "PowerOff interactive"}));
        QCOMPARE(finished.count(), 3);
        for (const auto &result : finished)
            QVERIFY(result.at(0).toBool());
        logind.refusal = "Another shutdown is already running.";
        QVERIFY(power.request(PowerControl::Action::ShutDown));
        QTRY_COMPARE(finished.count(), 4);
        QVERIFY(!finished.last().at(0).toBool());
        QCOMPARE(finished.last().at(1).toString(),
                 QString("The computer could not shut down. Another shutdown is already running."));
        service.unregisterService("org.freedesktop.login1");
        QDBusConnection::disconnectFromBus("logind");
        QDBusConnection::disconnectFromBus("desktop");
    }
    void withoutABusNothingIsRequested()
    {
        PowerControl power{QDBusConnection(QString())};
        QVERIFY(!power.available());
        QVERIFY(!power.request(PowerControl::Action::ShutDown));
        QVERIFY(!power.busy());
    }
};

QTEST_GUILESS_MAIN(PowerTests)
#include "power_tests.moc"
