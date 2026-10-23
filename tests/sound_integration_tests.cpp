#include "desktop/audio_service.hpp"
#include "desktop/manager.hpp"
#include "desktop/system_controls.hpp"
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>
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


class SoundIntegrationTests final : public QObject {
    Q_OBJECT
    static QQuickItem *item(QQuickItem *root, const QString &name) {
        if (root->objectName()==name) return root;
        for(auto *child:root->childItems()) if(auto *result=item(child,name)) return result;
        return nullptr;
    }
  private slots:
    void soundPanelOpensFromBothDesktopMenusAndOwnsModalInput() {
        QTest::failOnWarning(QRegularExpression(".*(Binding loop|ReferenceError|TypeError|is not a type).*"));
        Fixture fixture; QVERIFY(fixture.initialize());
        AudioService sound(fixture.helper());
        const auto controlsFile=fixture.directory.filePath("controls.py");
        QVERIFY(write(controlsFile,"import json\nprint(json.dumps({'state':{'volume':{'available':True,'value':50,'muted':False}}}))\n"));
        SystemControls controls(controlsFile);
        QQuickView view; view.resize(1120,760);
        NativeManager manager(nullptr,&view,fixture.directory.path(),fixture.directory.filePath("profile"),{});
        auto *owned=qobject_cast<AudioService *>(manager.audio()); QVERIFY(owned);
        QVERIFY(owned->state().isEmpty()); QVERIFY(!owned->busy());
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clear=qScopeGuard([&]{view.setSource({});});
        auto *panel=view.rootObject()->findChild<QObject *>("sound-panel"); QVERIFY(panel);
        auto *quick=view.rootObject()->findChild<QObject *>("quick-controls"); QVERIFY(quick);
        QVERIFY(panel->setProperty("service",QVariant::fromValue(static_cast<QObject *>(&sound))));
        QVERIFY(quick->setProperty("service",QVariant::fromValue(static_cast<QObject *>(&controls))));
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(fixture.commands().isEmpty()); QVERIFY(!manager.shellModal());
        auto click=[&](const QString &name){
            auto *target=item(view.contentItem(),name);
            if(!target || !target->isVisible() || !target->isEnabled()) return false;
            target->parentItem()->ensurePolished();
            QTest::mouseClick(&view,Qt::LeftButton,{},target->mapToScene(QPointF(target->width()/2,target->height()/2)).toPoint());
            return true;
        };
        QVERIFY(click("control-strip-sound"));
        QTRY_VERIFY(item(view.contentItem(),"desktop-command-sound"));
        QVERIFY(click("desktop-command-sound"));
        QTRY_VERIFY(panel->property("opened").toBool()); QVERIFY(manager.shellModal());
        QTRY_VERIFY(!sound.busy());
        QVERIFY2(sound.error().isEmpty(),qPrintable(sound.error()));
        QVERIFY(click("sound-device-bluez_output.headset"));
        QTRY_VERIFY(!sound.busy());
        QCOMPARE(sound.state().value("defaultOutput").toString(),QString("bluez_output.headset"));
        QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/integrated-sound.png"));
        QTest::keyClick(&view,Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QTRY_VERIFY(!panel->property("visible").toBool());
        const auto count=fixture.commands().size();
        QTest::qWait(2200); QCOMPARE(fixture.commands().size(),count);
        QTest::mouseClick(&view,Qt::LeftButton,{},QPoint(25,13));
        QTRY_VERIFY(item(view.contentItem(),"desktop-command-control-panels"));
        QVERIFY(click("desktop-command-control-panels"));
        QTRY_VERIFY(item(view.contentItem(),"desktop-command-sound"));
        QVERIFY(click("desktop-command-sound"));
        QTRY_VERIFY(panel->property("opened").toBool()); QVERIFY(manager.shellModal());
        QTest::keyClick(&view,Qt::Key_Escape); QTRY_VERIFY(!manager.shellModal());
        QVERIFY(owned->state().isEmpty()); QVERIFY(!owned->busy());
    }
};
QTEST_MAIN(SoundIntegrationTests)
#include "sound_integration_tests.moc"
