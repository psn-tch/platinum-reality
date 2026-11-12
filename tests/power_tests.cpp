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

// logind's own session ("auto": the caller's), whose Lock Special ▸ Lock Screen asks for.
class FakeSession final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Session")
  public:
    int locks = 0;
    QString refusal;

  public slots:
    void Lock()
    {
        ++locks;
        if (!refusal.isEmpty())
            sendErrorReply("org.freedesktop.DBus.Error.AccessDenied", refusal);
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
    void lockScreenAsksLogindToLockThisSession()
    {
        auto service = QDBusConnection::connectToBus(address, "logind");
        FakeSession session;
        QVERIFY(service.registerObject("/org/freedesktop/login1/session/auto", &session, QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService("org.freedesktop.login1"));
        PowerControl power(QDBusConnection::connectToBus(address, "desktop"));
        QSignalSpy finished(&power, &PowerControl::finished);
        QSignalSpy failed(&power, &PowerControl::lockFailed);
        QVERIFY(power.lockSession());
        QTRY_COMPARE(session.locks, 1);
        // Locking is not a power request: nothing waits on it.
        QVERIFY(!power.busy());
        session.refusal = "Not allowed.";
        QVERIFY(power.lockSession());
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QString("The screen could not be locked. Not allowed."));
        QCOMPARE(finished.count(), 0);
        service.unregisterService("org.freedesktop.login1");
        QDBusConnection::disconnectFromBus("logind");
        QDBusConnection::disconnectFromBus("desktop");
    }
    void withoutABusNothingIsRequested()
    {
        PowerControl power{QDBusConnection(QString())};
        QVERIFY(!power.available());
        QVERIFY(!power.lockSession());
        QVERIFY(!power.request(PowerControl::Action::ShutDown));
        QVERIFY(!power.busy());
    }
};

QTEST_GUILESS_MAIN(PowerTests)
#include "power_tests.moc"
