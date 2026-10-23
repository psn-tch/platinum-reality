#include "desktop/audio_service.hpp"
#include <memory>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QWheelEvent>
#include <QtTest>
#include <csignal>

namespace {
bool write(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
struct Fixture {
    QTemporaryDir directory;
    QString helper() const { return directory.filePath("helper with spaces.py"); }
    QString data() const { return directory.filePath("data.json"); }
    QString log() const { return directory.filePath("commands.jsonl"); }
    QJsonObject object() const {
        QFile file(data());
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    bool initialize() {
        QJsonObject state{{"outputs", QJsonArray{QJsonObject{{"name", "speakers"}, {"description", "Internal Speakers"}, {"volume", 50}, {"muted", false}, {"card", -1}},
                                                QJsonObject{{"name", "bluez_output.headset"}, {"description", "Test Headset"}, {"volume", 75}, {"muted", false}, {"card", 8}, {"bluetooth", true}}}},
                          {"inputs", QJsonArray{QJsonObject{{"name", "microphone"}, {"description", "Internal Microphone"}, {"volume", 60}, {"muted", false}, {"card", -1}},
                                               QJsonObject{{"name", "bluez_input.headset"}, {"description", "Headset Microphone"}, {"volume", 80}, {"muted", false}, {"card", 8}, {"bluetooth", true}}}},
                          {"cards", QJsonArray{QJsonObject{{"name", "headset"}, {"index", 8}, {"activeProfile", "a2dp-sink"},
                              {"profiles", QJsonArray{QJsonObject{{"name", "a2dp-sink"}, {"description", "High Fidelity Playback"}, {"available", true}},
                                                      QJsonObject{{"name", "headset-head-unit"}, {"description", "Headset with Microphone"}, {"available", true}},
                                                      QJsonObject{{"name", "unavailable"}, {"description", "Unavailable Profile"}, {"available", false}}}}}}},
                          {"defaultOutput", "speakers"}, {"defaultInput", "microphone"}};
        if (!write(data(), QJsonDocument(QJsonObject{{"state", state}, {"mode", "normal"}}).toJson())) return false;
        const auto literal = [](const QString &path) { return QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact) + "[0]"; };
        QByteArray script = "import json,os,sys,time\nfrom pathlib import Path\n"
            "data_path=Path(" + literal(data()) + ")\nlog_path=Path(" + literal(log()) + ")\n"
            "args=sys.argv[1:]\ndata=json.loads(data_path.read_text())\n"
            "with log_path.open('a') as f: f.write(json.dumps({'args':args,'pid':os.getpid()})+'\\n')\n"
            "mode=data.get('mode','normal')\ntime.sleep(2 if mode=='slow' else 0.08)\n"
            "if mode=='malformed': print('not json'); sys.exit(0)\n"
            "if mode=='empty': print('{}'); sys.exit(0)\n"
            "if mode=='fail': print(json.dumps({'error':'Fixture failure','state':{}})); sys.exit(1)\n"
            "s=data['state']\n"
            "if args[0]=='output': s['defaultOutput']=args[1]\n"
            "elif args[0]=='input': s['defaultInput']=args[1]\n"
            "elif args[0]=='profile': next(r for r in s['cards'] if r['name']==args[1])['activeProfile']=args[2]\n"
            "elif args[0] in ('volume','mute'):\n"
            " r=next(r for r in s['outputs' if args[1]=='output' else 'inputs'] if r['name']==args[2])\n"
            " r['volume' if args[0]=='volume' else 'muted']=int(args[3]) if args[0]=='volume' else bool(int(args[3]))\n"
            "s['pythonPathInherited']='PYTHONPATH' in os.environ or 'PYTHONHOME' in os.environ\n"
            "data_path.write_text(json.dumps(data))\n"
            "print(json.dumps({'state':s,'warning':'An application could not move.' if mode=='warning' else ''}))\n";
        return write(helper(), script);
    }
    bool mode(const QString &mode) {
        auto value = object(); value.insert("mode", mode);
        return write(data(), QJsonDocument(value).toJson());
    }
    QList<QJsonObject> commands() const {
        QFile file(log()); if (!file.open(QIODevice::ReadOnly)) return {};
        QList<QJsonObject> result;
        for (const auto &line : file.readAll().split('\n')) {
            const auto value = QJsonDocument::fromJson(line).object();
            if (!value.isEmpty()) result << value;
        }
        return result;
    }
};
}

class AudioServiceTests final : public QObject
{
    Q_OBJECT
    static QQuickItem *item(QQuickItem *root, const QString &name) {
        if (root->objectName() == name) return root;
        for (auto *child : root->childItems())
            if (auto *result = item(child, name)) return result;
        return nullptr;
    }
    static QPoint center(QQuickItem *item) {
        return item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
    }
  private slots:
    void init() { QTest::failOnWarning(QRegularExpression(".*Binding loop.*")); }
    void refreshUsesTheHelperAndDoesNotInheritPythonImports()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        const auto oldPath = qgetenv("PYTHONPATH"), oldHome = qgetenv("PYTHONHOME");
        const auto restore = qScopeGuard([&] {
            oldPath.isNull() ? qunsetenv("PYTHONPATH") : qputenv("PYTHONPATH", oldPath);
            oldHome.isNull() ? qunsetenv("PYTHONHOME") : qputenv("PYTHONHOME", oldHome);
        });
        qputenv("PYTHONPATH", "/untrusted/python"); qputenv("PYTHONHOME", "/untrusted/home");
        AudioService service(fixture.helper());
        service.refresh(); QVERIFY(service.busy());
        QTRY_VERIFY(!service.busy());
        QVERIFY2(service.error().isEmpty(), qPrintable(service.error()));
        QCOMPARE(service.state().value("defaultOutput").toString(), QString("speakers"));
        QVERIFY(!service.state().value("pythonPathInherited").toBool());
        QCOMPARE(fixture.commands().size(), 1);
        QCOMPARE(fixture.commands().first().value("args").toArray(), QJsonArray{"status"});
    }
    void newestQueuedOutputSurvivesAnOlderReply()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        service.refresh(); QTRY_VERIFY(!service.busy());
        service.chooseOutput("first");
        service.chooseOutput("second");
        service.chooseOutput("last");
        QCOMPARE(service.state().value("defaultOutput").toString(), QString("last"));
        bool regressed = false;
        connect(&service, &AudioService::stateChanged, &service, [&] {
            if (service.state().value("defaultOutput").toString() != "last") regressed = true;
        });
        QTRY_VERIFY(!service.busy());
        QVERIFY(!regressed);
        const auto commands = fixture.commands();
        QCOMPARE(commands.size(), 3);
        QCOMPARE(commands.at(1).value("args").toArray(), QJsonArray({"output", "first"}));
        QCOMPARE(commands.at(2).value("args").toArray(), QJsonArray({"output", "last"}));
        QCOMPARE(fixture.object().value("state").toObject().value("defaultOutput").toString(), QString("last"));
    }
    void volumeChangesCoalesceWhileRetainingTheLatestSliderPosition()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        service.refresh(); QTRY_VERIFY(!service.busy());
        service.setVolume("output", "speakers", 30);
        service.setVolume("output", "speakers", 31);
        service.setVolume("output", "speakers", 32);
        QCOMPARE(service.state().value("outputs").toList().first().toMap().value("volume").toInt(), 32);
        QTRY_VERIFY(!service.busy());
        QCOMPARE(service.state().value("outputs").toList().first().toMap().value("volume").toInt(), 32);
        QCOMPARE(fixture.commands().size(), 3);
    }
    void independentChoicesKeepTheirOrderAndRefreshOnlyOnce()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        service.chooseOutput("headset");
        service.chooseInput("microphone");
        service.setVolume("output", "speakers", 65);
        service.refresh(); service.refresh();
        QTRY_VERIFY(!service.busy());
        const auto commands = fixture.commands();
        QCOMPARE(commands.size(), 4);
        QCOMPARE(commands.at(0).value("args").toArray().at(0).toString(), QString("output"));
        QCOMPARE(commands.at(1).value("args").toArray().at(0).toString(), QString("input"));
        QCOMPARE(commands.at(2).value("args").toArray().at(0).toString(), QString("volume"));
        QCOMPARE(commands.at(3).value("args").toArray(), QJsonArray{"status"});
    }
    void errorsClearQueuedActionsAndRestoreTheReportedSelection()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        service.refresh(); QTRY_VERIFY(!service.busy());
        QVERIFY(fixture.mode("fail"));
        service.chooseOutput("first"); service.chooseOutput("last");
        QTRY_VERIFY(!service.busy());
        QCOMPARE(service.error(), QString("Fixture failure"));
        QCOMPARE(service.state().value("defaultOutput").toString(), QString("speakers"));
        QCOMPARE(fixture.commands().size(), 2);
    }
    void malformedRepliesDoNotReplaceTheLastDeviceSnapshot()
    {
        for (const auto &mode : {QString("malformed"), QString("empty")}) {
            Fixture fixture; QVERIFY(fixture.initialize());
            AudioService service(fixture.helper());
            service.refresh(); QTRY_VERIFY(!service.busy());
            QVERIFY(fixture.mode(mode));
            service.chooseOutput("new"); service.chooseInput("new");
            QTRY_VERIFY(!service.busy());
            QVERIFY(!service.error().isEmpty());
            QCOMPARE(service.state().value("defaultOutput").toString(), QString("speakers"));
            QCOMPARE(fixture.commands().size(), 2);
        }
    }
    void timeoutStopsTheHelperAndDoesNotRetryQueuedChanges()
    {
        Fixture fixture; QVERIFY(fixture.initialize()); QVERIFY(fixture.mode("slow"));
        AudioService service(fixture.helper(), nullptr, 100);
        service.chooseOutput("new"); service.chooseInput("microphone");
        QTRY_VERIFY(!service.busy());
        QVERIFY(!service.error().isEmpty());
        QCOMPARE(fixture.commands().size(), 1);
        const auto pid = fixture.commands().first().value("pid").toInteger();
        QVERIFY(::kill(pid, 0) != 0);
    }
    void invalidRequestsAndMissingHelpersNeverStartAProcess()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        service.chooseOutput("--help"); QVERIFY(!service.error().isEmpty());
        service.chooseInput("a;id"); QVERIFY(!service.error().isEmpty());
        service.setVolume("output", "speakers", 101); QVERIFY(!service.error().isEmpty());
        service.setMuted("shell", "speakers", true); QVERIFY(!service.error().isEmpty());
        QVERIFY(!service.busy()); QVERIFY(fixture.commands().isEmpty());
        AudioService absent(fixture.directory.filePath("absent.py"));
        absent.refresh(); QVERIFY(!absent.busy()); QVERIFY(!absent.error().isEmpty());
    }
    void partialMoveWarningsRemainVisibleWithoutReportingAFullFailure()
    {
        Fixture fixture; QVERIFY(fixture.initialize()); QVERIFY(fixture.mode("warning"));
        AudioService service(fixture.helper());
        service.chooseOutput("headset"); QTRY_VERIFY(!service.busy());
        QVERIFY(service.error().isEmpty()); QVERIFY(!service.warning().isEmpty());
        QCOMPARE(service.state().value("defaultOutput").toString(), QString("headset"));
    }
    void destructorStopsOnlyItsOwnPendingHelper()
    {
        Fixture fixture; QVERIFY(fixture.initialize()); QVERIFY(fixture.mode("slow"));
        qint64 pid = 0;
        {
            AudioService service(fixture.helper()); service.refresh();
            QTRY_VERIFY(!fixture.commands().isEmpty());
            pid = fixture.commands().first().value("pid").toInteger();
            QVERIFY(::kill(pid, 0) == 0);
        }
        QVERIFY(::kill(pid, 0) != 0);
    }
    void stoppingTheHelperAlsoStopsItsSubprocess_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("timeout") << false;
        QTest::newRow("destructor") << true;
    }
    void stoppingTheHelperAlsoStopsItsSubprocess()
    {
        QFETCH(bool, destroy);
        Fixture fixture; QVERIFY(fixture.initialize());
        const auto marker = fixture.directory.filePath("descendant.json");
        const auto literal = QJsonDocument(QJsonArray{marker}).toJson(QJsonDocument::Compact) + "[0]";
        QVERIFY(write(fixture.helper(), "import json,os,subprocess,sys,time\nfrom pathlib import Path\n"
            "marker=" + literal + "\n"
            "child=subprocess.Popen([sys.executable,'-I','-c','import time; time.sleep(60)',marker],"
            "stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)\n"
            "Path(marker).write_text(json.dumps({'parent':os.getpid(),'child':child.pid}))\n"
            "time.sleep(60)\n"));
        qint64 child = 0;
        const auto cleanup = qScopeGuard([&] {
            QFile arguments(QString("/proc/%1/cmdline").arg(child));
            if (child > 0 && arguments.open(QIODevice::ReadOnly) && arguments.readAll().split('\0').contains(marker.toUtf8()))
                ::kill(child, SIGKILL);
        });
        auto service = std::make_unique<AudioService>(fixture.helper(), nullptr, destroy ? 25000 : 300);
        service->refresh();
        QTRY_VERIFY(QFile::exists(marker));
        QFile metadata(marker); QVERIFY(metadata.open(QIODevice::ReadOnly));
        child = QJsonDocument::fromJson(metadata.readAll()).object().value("child").toInteger();
        QVERIFY(child > 0);
        QVERIFY(::kill(child, 0) == 0);
        if (destroy) service.reset();
        else { QTRY_VERIFY(!service->busy()); QVERIFY(!service->error().isEmpty()); }
        const auto stopped = [&] {
            QFile status(QString("/proc/%1/stat").arg(child));
            if (!status.open(QIODevice::ReadOnly)) return true;
            const auto bytes = status.readAll();
            return bytes.mid(bytes.lastIndexOf(')') + 2, 1) == "Z";
        };
        QTRY_VERIFY_WITH_TIMEOUT(stopped(), 5000); // A loaded machine (a fresh VM's package build) needed just over 1 s.
    }
    void oversizedRepliesStopBeforeTheHelperFinishes()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        QVERIFY(write(fixture.helper(), "import os,time\n"
            "for _ in range(768): os.write(1,b'x'*4096)\n"
            "time.sleep(60)\n"));
        AudioService service(fixture.helper());
        service.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 1000);
        QVERIFY(!service.error().isEmpty());
        QVERIFY(service.state().isEmpty());
        QVERIFY(fixture.initialize());
        service.refresh();
        QTRY_VERIFY(!service.busy());
        QVERIFY2(service.error().isEmpty(), qPrintable(service.error()));
        QCOMPARE(service.state().value("defaultOutput").toString(), QString("speakers"));
    }
    void soundPanelSelectsDevicesProfilesAndLevelsAndReleasesModalInput()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        QQuickView view;
        view.rootContext()->setContextProperty("audioService", &service);
        view.setSource(QUrl::fromLocalFile(QString(AUDIO_PREVIEW_FILE)));
        QVERIFY2(view.status() == QQuickView::Ready, qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(!service.busy() && service.state().contains("outputs"));
        auto *session = view.rootObject()->findChild<QObject *>("fixture-session");
        QVERIFY(session); QCOMPARE(session->property("activeTokens").toInt(), 1);
        QTRY_VERIFY(item(view.contentItem(), "sound-device-bluez_output.headset"));
        auto *headset = item(view.contentItem(), "sound-device-bluez_output.headset");
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(headset));
        QTRY_COMPARE(fixture.object().value("state").toObject().value("defaultOutput").toString(), QString("bluez_output.headset"));
        QTRY_VERIFY(!service.busy());
        auto *level = item(view.contentItem(), "sound-volume"); QVERIFY(level);
        level->forceActiveFocus();
        for (int i = 0; i < 5; ++i) QTest::keyClick(&view, Qt::Key_Right);
        QTRY_COMPARE(fixture.object().value("state").toObject().value("outputs").toArray().at(1).toObject().value("volume").toInt(), 80);
        QTRY_VERIFY(!service.busy());
        auto *mute = item(view.contentItem(), "sound-mute"); QVERIFY(mute);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(mute));
        QTRY_VERIFY(fixture.object().value("state").toObject().value("outputs").toArray().at(1).toObject().value("muted").toBool());
        QTRY_VERIFY(!service.busy());
        auto *profile = item(view.contentItem(), "sound-profile"); QVERIFY(profile); QVERIFY(profile->isVisible());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(profile));
        auto *menu = view.rootObject()->findChild<QObject *>("sound-profile-menu");
        QVERIFY(menu); QTRY_VERIFY(menu->property("opened").toBool());
        QCOMPARE(menu->property("count").toInt(), 3);
        QTRY_VERIFY(item(view.contentItem(), "sound-profile-headset-head-unit"));
        auto *option = item(view.contentItem(), "sound-profile-headset-head-unit");
        auto *unavailable = item(view.contentItem(), "sound-profile-unavailable");
        QVERIFY(unavailable); QVERIFY(!unavailable->isEnabled());
        QVERIFY(option->isVisible()); QVERIFY(option->width() > 0 && option->height() > 0);
        QDir().mkpath(AUDIO_ARTIFACT_DIRECTORY);
        QVERIFY(view.grabWindow().save(QString(AUDIO_ARTIFACT_DIRECTORY) + "/sound-profile-menu.png"));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(option));
        QTRY_COMPARE(fixture.object().value("state").toObject().value("cards").toArray().first().toObject().value("activeProfile").toString(), QString("headset-head-unit"));
        QTRY_VERIFY(!service.busy());
        QDir().mkpath(AUDIO_ARTIFACT_DIRECTORY);
        QTest::qWait(100);
        QVERIFY(view.grabWindow().save(QString(AUDIO_ARTIFACT_DIRECTORY) + "/sound-panel.png"));
        auto *input = item(view.contentItem(), "sound-tab-input"); QVERIFY(input);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(input));
        QTRY_VERIFY(item(view.contentItem(), "sound-device-bluez_input.headset"));
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(item(view.contentItem(), "sound-device-bluez_input.headset")));
        QTRY_COMPARE(fixture.object().value("state").toObject().value("defaultInput").toString(), QString("bluez_input.headset"));
        QTRY_VERIFY(!service.busy());
        QVERIFY(view.grabWindow().save(QString(AUDIO_ARTIFACT_DIRECTORY) + "/sound-input.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_COMPARE(session->property("activeTokens").toInt(), 0);
    }
    void pendingWheelEditsStayWithTheirDeviceAcrossTabsAndClosing()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        QQuickView view;
        view.rootContext()->setContextProperty("audioService", &service);
        view.setSource(QUrl::fromLocalFile(QString(AUDIO_PREVIEW_FILE)));
        QVERIFY(view.status() == QQuickView::Ready);
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(!service.busy() && service.state().contains("outputs"));
        auto *level = item(view.contentItem(), "sound-volume"); QVERIFY(level);
        const auto wheel = [&] {
            const auto point = center(level);
            QWheelEvent event(point, view.mapToGlobal(point), {}, {0, 120}, Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&view, &event);
        };
        wheel();
        QCOMPARE(level->property("editTarget").toString(), QString("speakers"));
        const auto outputValue = level->property("pendingValue").toInt();
        QVERIFY(outputValue > 50);
        auto *input = item(view.contentItem(), "sound-tab-input"); QVERIFY(input);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(input));
        wheel();
        QCOMPARE(level->property("editTarget").toString(), QString("microphone"));
        const auto inputValue = level->property("pendingValue").toInt();
        QVERIFY(inputValue > 60);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!service.busy());
        const auto state = fixture.object().value("state").toObject();
        QCOMPARE(state.value("outputs").toArray().first().toObject().value("volume").toInt(), outputValue);
        QCOMPARE(state.value("inputs").toArray().first().toObject().value("volume").toInt(), inputValue);
        auto *session = view.rootObject()->findChild<QObject *>("fixture-session"); QVERIFY(session);
        QTRY_COMPARE(session->property("activeTokens").toInt(), 0);
    }
    void removingTheSelectedHeadsetClosesItsModeMenuAndDisablesMissingControls()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        QQuickView view;
        view.rootContext()->setContextProperty("audioService", &service);
        view.setSource(QUrl::fromLocalFile(QString(AUDIO_PREVIEW_FILE)));
        QVERIFY(view.status() == QQuickView::Ready);
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(!service.busy() && service.state().contains("outputs"));
        service.chooseOutput("bluez_output.headset"); QTRY_VERIFY(!service.busy());
        auto *profile = item(view.contentItem(), "sound-profile"); QVERIFY(profile);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(profile));
        auto *menu = view.rootObject()->findChild<QObject *>("sound-profile-menu"); QVERIFY(menu);
        QTRY_VERIFY(menu->property("opened").toBool());
        auto data = fixture.object();
        auto state = data.value("state").toObject();
        state.insert("outputs", QJsonArray{}); state.insert("cards", QJsonArray{}); state.insert("defaultOutput", "");
        data.insert("state", state); QVERIFY(write(fixture.data(), QJsonDocument(data).toJson()));
        service.refresh(); QTRY_VERIFY(!service.busy());
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(!item(view.contentItem(), "sound-volume")->isEnabled());
        QVERIFY(!item(view.contentItem(), "sound-mute")->isEnabled());
        QVERIFY(!profile->isVisible());
        QVERIFY(service.error().isEmpty());
        QTest::keyClick(&view, Qt::Key_Escape);
    }
    void anExternalVolumeRefreshDoesNotPullAwayAHeldSlider()
    {
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService service(fixture.helper());
        QQuickView view;
        view.rootContext()->setContextProperty("audioService", &service);
        view.setSource(QUrl::fromLocalFile(QString(AUDIO_PREVIEW_FILE)));
        QVERIFY(view.status() == QQuickView::Ready);
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(!service.busy() && service.state().contains("outputs"));
        auto *level = item(view.contentItem(), "sound-volume"); QVERIFY(level);
        const auto start = center(level);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&view, start + QPoint(60, 0));
        QTRY_VERIFY(level->property("pressed").toBool());
        const auto wanted = qRound(level->property("value").toDouble());
        QVERIFY(wanted > 50);
        auto data = fixture.object(); auto state = data.value("state").toObject();
        auto outputs = state.value("outputs").toArray(); auto speaker = outputs.first().toObject();
        speaker.insert("volume", 30); outputs[0] = speaker; state.insert("outputs", outputs);
        data.insert("state", state); QVERIFY(write(fixture.data(), QJsonDocument(data).toJson()));
        QSignalSpy refreshed(&service, &AudioService::stateChanged);
        service.refresh(); QTRY_VERIFY(!service.busy());
        QTRY_VERIFY(!refreshed.isEmpty());
        QCOMPARE(service.state().value("outputs").toList().first().toMap().value("volume").toInt(), 30);
        QCOMPARE(qRound(level->property("value").toDouble()), wanted);
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, start + QPoint(60, 0));
        QTRY_COMPARE(fixture.object().value("state").toObject().value("outputs").toArray().first().toObject().value("volume").toInt(), wanted);
        QTRY_VERIFY(!service.busy());
        QTest::keyClick(&view, Qt::Key_Escape);
    }
};
QTEST_MAIN(AudioServiceTests)
#include "audio_service_tests.moc"
