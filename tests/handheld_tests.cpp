#include "desktop/handheld.hpp"
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

// The Handheld module's service against a fake Handheld Daemon serving what hhd 4.1.12 reports
// on a OneXPlayer G1 (tests/fixtures/hhd-oxp-g1.json). Nothing here reaches the host's hhd.
class HandheldTests final : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;
    QProcess fake;
    QUrl base;
    QString tokenFile;
    QStringList posts()
    {
        QFile log(directory.filePath("posts.log"));
        return log.open(QIODevice::ReadOnly) ? QString::fromUtf8(log.readAll()).split('\n', Qt::SkipEmptyParts)
                                             : QStringList();
    }
    static QStringList titles(const QVariant &item)
    {
        QStringList result;
        for (const auto &option : item.toMap().value("options").toList())
            result << option.toMap().value("title").toString();
        return result;
    }

  private slots:
    void initTestCase()
    {
        tokenFile = directory.filePath("token");
        QFile token(tokenFile);
        QVERIFY(token.open(QIODevice::WriteOnly) && token.write("abc123def456\n") > 0);
        token.close();
        fake.start("python3", {"-B", QFINDTESTDATA("fake_hhd.py"), QFINDTESTDATA("fixtures/hhd-oxp-g1.json"),
                               "abc123def456", directory.filePath("posts.log")});
        QVERIFY(fake.waitForStarted() && fake.waitForReadyRead(5000));
        base = QUrl("http://127.0.0.1:" + QString::fromUtf8(fake.readLine()).trimmed());
    }
    void cleanupTestCase()
    {
        fake.kill();
        fake.waitForFinished(3000);
    }
    void desktopTestsNeverUseTheHostEndpointEvenWithAPrivateSystemBus()
    {
        QProcessEnvironment environment;
        environment.insert("FRANKENSTEIN_NATIVE_TEST", "1");
        environment.insert("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/tmp/handheld-fixture-no-system-bus");
        auto endpoint = desktopHandheldEndpoint(true, environment);
        QVERIFY(endpoint.tokenFile.isEmpty());
        QVERIFY(endpoint.base.port() != 5335);
        HandheldDaemon disabled(endpoint.base, endpoint.tokenFile);
        QSignalSpy changed(&disabled, &HandheldDaemon::changed);
        disabled.refresh();
        QVERIFY(!disabled.available() && !disabled.busy() && changed.count() >= 2);

        // The one explicit fake is still usable, even with the host-only opt-out.
        environment.insert("PLATINUM_DISABLE_HOST_HANDHELD", "1");
        environment.insert("FRANKENSTEIN_TEST_HHD", base.toString()+" "+tokenFile);
        endpoint = desktopHandheldEndpoint(true, environment);
        QCOMPARE(endpoint.base, base);
        QCOMPARE(endpoint.tokenFile, tokenFile);
        HandheldDaemon fixture(endpoint.base, endpoint.tokenFile);
        fixture.refresh();
        QTRY_VERIFY(fixture.available() && !fixture.busy());
        QCOMPARE(fixture.state().value("controller").toMap().value("value").toString(), QString("hidden"));
    }
    void installedFixturesCanOptOutWithoutEnablingNativeTestMode()
    {
        QProcessEnvironment environment;
        environment.insert("PLATINUM_DISABLE_HOST_HANDHELD", "1");
        environment.insert("FRANKENSTEIN_TEST_HHD", base.toString()+" "+tokenFile);
        auto endpoint = desktopHandheldEndpoint(true, environment);
        QVERIFY(endpoint.tokenFile.isEmpty()); // an unguarded fake override is ignored
        environment.remove("PLATINUM_DISABLE_HOST_HANDHELD");
        endpoint = desktopHandheldEndpoint(true, environment);
        QCOMPARE(endpoint.base, QUrl("http://127.0.0.1:5335"));
        QCOMPARE(endpoint.tokenFile, QString("/etc/hhd/.token")); // inspect policy only: never read it
        QVERIFY(desktopHandheldEndpoint(false, environment).tokenFile.isEmpty());
    }
    void readsWhatHhdOffersForThisDevice()
    {
        HandheldDaemon hhd(base, tokenFile);
        hhd.refresh();
        QTRY_VERIFY(hhd.available() && !hhd.busy());
        const auto state = hhd.state();
        QCOMPARE(state.value("controller").toMap().value("key").toString(),
                 QString("controllers.oxp.controller_mode.mode"));
        QCOMPARE(state.value("controller").toMap().value("value").toString(), QString("hidden"));
        // The familiar controller modes first, in hhd's titles.
        QCOMPARE(titles(state.value("controller")),
                 QStringList({"Xbox", "Xbox Elite", "Dualsense", "Hidden", "Paused"}));
        const auto tdp = state.value("tdp").toMap();
        QCOMPARE(tdp.value("value").toInt(), 8);
        QCOMPARE(tdp.value("min").toInt(), 4);
        QCOMPARE(tdp.value("max").toInt(), 30);
        QCOMPARE(state.value("boost").toMap().value("value").toBool(), false);
        QCOMPARE(state.value("fan").toMap().value("value").toString(), QString("disabled"));
        QCOMPARE(titles(state.value("chargeLimit")), QStringList({"70%", "80%", "85%", "90%", "95%", "Unset"}));
        QVERIFY(posts().isEmpty()); // Reading changes nothing.
    }
    void changesOnlyWhatTheModuleOffers()
    {
        HandheldDaemon hhd(base, tokenFile);
        hhd.refresh();
        QTRY_VERIFY(hhd.available() && !hhd.busy());
        hhd.set("controllers.oxp.controller_mode.mode", "uinput");
        QTRY_COMPARE(hhd.state().value("controller").toMap().value("value").toString(), QString("uinput"));
        QTRY_VERIFY(!hhd.busy());
        hhd.set("tdp.qam.tdp", 15.0); // QML numbers arrive as doubles; hhd wants an integer.
        QTRY_COMPARE(hhd.state().value("tdp").toMap().value("value").toInt(), 15);
        QTRY_VERIFY(!hhd.busy());
        hhd.set("tdp.qam.boost", true);
        QTRY_VERIFY(hhd.state().value("boost").toMap().value("value").toBool());
        QTRY_VERIFY(!hhd.busy());
        QCOMPARE(posts(), QStringList({R"({"controllers.oxp.controller_mode.mode": "uinput"})",
                                       R"({"tdp.qam.tdp": 15})", R"({"tdp.qam.boost": true})"}));
        // Outside hhd's limits or choices, or a setting the module doesn't offer: nothing is sent.
        hhd.set("tdp.qam.tdp", 31);
        hhd.set("controllers.oxp.controller_mode.mode", "not-a-mode");
        hhd.set("tdp.qam.boost", "yes");
        hhd.set("tdp.smu.std.fast_limit", 54);
        QTest::qWait(300);
        QCOMPARE(posts().size(), 3);
    }
    void aChangeDuringARefreshIsNotUndoneByIt()
    {
        HandheldDaemon hhd(base, tokenFile);
        hhd.refresh();
        QTRY_VERIFY(hhd.available() && !hhd.busy());
        // The module refreshes as it opens; a choice made at once is sent, and the refresh's older
        // answer does not put the previous value back.
        hhd.refresh();
        QVERIFY(!hhd.busy());
        hhd.set("tdp.qam.tdp", 20);
        QTRY_COMPARE(hhd.state().value("tdp").toMap().value("value").toInt(), 20);
        QTRY_VERIFY(!hhd.busy());
        QTest::qWait(300);
        QCOMPARE(hhd.state().value("tdp").toMap().value("value").toInt(), 20);
        QVERIFY(posts().contains(R"({"tdp.qam.tdp": 20})"));
    }
    void withoutTheRightTokenItIsUnavailable()
    {
        QFile wrong(directory.filePath("wrong-token"));
        QVERIFY(wrong.open(QIODevice::WriteOnly) && wrong.write("nope") > 0);
        wrong.close();
        HandheldDaemon refused(base, wrong.fileName());
        QSignalSpy changed(&refused, &HandheldDaemon::changed);
        refused.refresh();
        QTRY_VERIFY(!refused.busy() && changed.count() >= 2);
        QVERIFY(!refused.available());
        HandheldDaemon missing(base, directory.filePath("absent"));
        missing.refresh();
        QTRY_VERIFY(!missing.busy());
        QVERIFY(!missing.available());
    }
};

QTEST_GUILESS_MAIN(HandheldTests)
#include "handheld_tests.moc"
