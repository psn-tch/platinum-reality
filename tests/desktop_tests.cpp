#include "applications/application_catalog.hpp"
#include "desktop/appearance_settings.hpp"
#include "desktop/alert_sounds.hpp"
#include "desktop/battery_status.hpp"
#include "desktop/interface_sounds.hpp"
#include "desktop/command_keys.hpp"
#include "desktop/audio_service.hpp"
#include "desktop/desktop_items.hpp"
#include "desktop/display_layout.hpp"
#include "desktop/drop_receiver_window.hpp"
#include "desktop/manager.hpp"
#include "desktop/surface_client.hpp"
#include "desktop/screenshot_service.hpp"
#include "desktop/system_controls.hpp"
#include "finder/file_drag.hpp"
#include "native_keys.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QtTest>
#include <qpa/qplatformdrag.h>

namespace
{
// Deferred wire data, as returned by a platform MIME provider. The advertised
// standard URL format deliberately reads empty, matching the failed private-bus
// portal path; the owned payload still contains the original encoded URLs.
class PortalUnavailableFileMime final : public QMimeData
{
  public:
    QHash<QString, QByteArray> payload;
    mutable QStringList reads;
    QStringList formats() const override
    {
        return payload.keys();
    }

  protected:
    QVariant retrieveData(const QString &format, QMetaType) const override
    {
        reads.push_back(format);
        return payload.value(format);
    }
};
} // namespace

class DesktopTests : public QObject
{
    Q_OBJECT
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
    void commandChordsBecomeTheApplicationsOwnShortcuts()
    {
        const auto command = Qt::MetaModifier;
        const auto chord = [](uint key, Qt::KeyboardModifiers modifiers, bool terminal = false) {
            const auto translated = translateCommandKey(key, modifiers, terminal);
            return translated ? QString("%1%2%3").arg(translated->control ? "ctrl+" : "",
                                                      translated->shift ? "shift+" : "")
                                    .arg(translated->key)
                              : QString("-");
        };
        // Copy, paste, cut, select all, undo; Shift kept for redo.
        QCOMPARE(chord(46, command), QString("ctrl+46"));
        QCOMPARE(chord(47, command), QString("ctrl+47"));
        QCOMPARE(chord(45, command), QString("ctrl+45"));
        QCOMPARE(chord(30, command), QString("ctrl+30"));
        QCOMPARE(chord(44, command | Qt::ShiftModifier), QString("ctrl+shift+44"));
        // Terminals copy and paste with Control-Shift; Control-C still interrupts.
        QCOMPARE(chord(46, command, true), QString("ctrl+shift+46"));
        QCOMPARE(chord(47, command, true), QString("ctrl+shift+47"));
        QCOMPARE(chord(2, command, true), QString("ctrl+2"));
        // Digits and punctuation; Command-period cancels.
        QCOMPARE(chord(2, command), QString("ctrl+2"));
        QCOMPARE(chord(51, command), QString("ctrl+51"));
        QCOMPARE(chord(52, command), QString("1"));
        // Line and document navigation, with Shift selecting.
        QCOMPARE(chord(105, command), QString("102"));
        QCOMPARE(chord(106, command | Qt::ShiftModifier), QString("shift+107"));
        QCOMPARE(chord(103, command), QString("ctrl+102"));
        QCOMPARE(chord(108, command), QString("ctrl+107"));
        // Untranslated: no Command, other modifiers, and keys without a meaning.
        QCOMPARE(chord(46, Qt::ControlModifier), QString("-"));
        QCOMPARE(chord(46, command | Qt::ControlModifier), QString("-"));
        QCOMPARE(chord(46, command | Qt::AltModifier), QString("-"));
        QCOMPARE(chord(15, command), QString("-")); // Tab
        QCOMPARE(chord(57, command), QString("-")); // Space
        QCOMPARE(chord(14, command), QString("-")); // Backspace
        QCOMPARE(chord(1, command), QString("-"));  // Escape
        QCOMPARE(chord(59, command), QString("-")); // F1
        QVERIFY(isTerminalApplication("org.kde.konsole"));
        QVERIFY(isTerminalApplication("Alacritty"));
        QVERIFY(isTerminalApplication("foot.desktop"));
        QVERIFY(!isTerminalApplication("chromium"));
        QVERIFY(!isTerminalApplication(""));
    }
    void aSurfaceWithoutALiveClientHasNoPeerIdentity()
    {
        QCOMPARE(liveSurfaceClientPid(nullptr), qint64(0));
        QWaylandSurface unattached;
        QVERIFY(!unattached.client());
        QCOMPARE(liveSurfaceClientPid(&unattached), qint64(0));
    }
    void hardwareVolumeAndBrightnessKeysStepTheControls()
    {
        // The laptop's volume, mute and brightness keys go through the Quick Controls service;
        // a stateful fake helper stands in, so no real level ever changes.
        QTemporaryDir root;
        const auto helper = root.filePath("controls.py");
        QFile script(helper);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("import json,sys\nfrom pathlib import Path\n"
                     "here=Path(__file__);store=here.with_suffix('.state')\n"
                     "state=json.loads(store.read_text()) if store.exists() else {"
                     "'volume':{'available':True,'value':70,'muted':False},'brightness':{'available':True,'value':40},"
                     "'wifi':{'available':False},'bluetooth':{'available':False}}\n"
                     "with here.with_suffix('.calls').open('a') as calls: calls.write(' '.join(sys.argv[1:])+'\\n')\n"
                     "if sys.argv[1]=='set':\n"
                     "    control,value=sys.argv[2],int(sys.argv[3])\n"
                     "    if control=='mute': state['volume']['muted']=bool(value)\n"
                     "    else: state[control]['value']=value\n"
                     "    store.write_text(json.dumps(state))\n"
                     "print(json.dumps({'error':'','state':state}))\n");
        script.close();
        const auto calls = [&] {
            QFile file(root.filePath("controls.calls"));
            return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed().split('\n') : QByteArrayList{};
        };
        const auto last = [&] { const auto all = calls(); return all.isEmpty() ? QByteArray() : all.last(); };
        SystemControls controls(helper);
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *panel = view.rootObject()->findChild<QObject *>("quick-controls");
        QVERIFY(panel);
        QVERIFY(panel->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&controls))));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *indicator = visualItem(view.contentItem(), "level-indicator");
        QVERIFY(indicator && !indicator->isVisible());
        const auto settle = [&] { return QTest::qWaitFor([&] { return !controls.busy(); }, 5000); };
        // The first press reads the levels, then applies itself.
        QTest::keyClick(&view, Qt::Key_VolumeUp);
        QTRY_COMPARE(last(), QByteArray("set volume 75"));
        QCOMPARE(calls().value(0), QByteArray("status"));
        QVERIFY(indicator->isVisible());
        QCOMPARE(indicator->property("control").toString(), QString("volume"));
        QCOMPARE(indicator->property("value").toInt(), 75);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/level-indicator.png"));
        QVERIFY(settle());
        QTest::keyClick(&view, Qt::Key_VolumeDown);
        QTRY_COMPARE(last(), QByteArray("set volume 70"));
        QVERIFY(settle());
        QTest::keyClick(&view, Qt::Key_VolumeMute);
        QTRY_COMPARE(last(), QByteArray("set mute 1"));
        QVERIFY(indicator->property("muted").toBool());
        QVERIFY(settle());
        // Turning the volume up again unmutes, as on other desktops.
        QTest::keyClick(&view, Qt::Key_VolumeUp);
        QTRY_COMPARE(last(), QByteArray("set volume 75"));
        QVERIFY(calls().contains("set mute 0"));
        QVERIFY(settle());
        QTest::keyClick(&view, Qt::Key_MonBrightnessUp);
        QTRY_COMPARE(last(), QByteArray("set brightness 45"));
        QCOMPARE(indicator->property("control").toString(), QString("brightness"));
        QVERIFY(settle());
        // Brightness never goes below 5 %, so the screen cannot be blacked out by the keys.
        for (int press = 0; press < 12; ++press)
        {
            QTest::keyClick(&view, Qt::Key_MonBrightnessDown);
            QVERIFY(settle());
        }
        QTRY_COMPARE(last(), QByteArray("set brightness 5"));
        QCOMPARE(controls.state().value("brightness").toMap().value("value").toInt(), 5);
        QTRY_VERIFY_WITH_TIMEOUT(!indicator->isVisible(), 4000);
    }
    void quickControlsUseAnAsynchronousFakeAndReleaseModalInput()
    {
        QTemporaryDir root;
        const auto helper=root.filePath("controls.py");
        QFile script(helper);
        QVERIFY(script.open(QIODevice::WriteOnly));
        // A stateful fake: settings persist between calls and a level change
        // takes long enough for further keyboard changes to arrive while busy.
        script.write("import json,sys,time\nfrom pathlib import Path\n"
                     "here=Path(__file__);store=here.with_suffix('.state')\n"
                     "state=json.loads(store.read_text()) if store.exists() else {"
                     "'volume':{'available':True,'value':70,'muted':False},'brightness':{'available':True,'value':40},"
                     "'wifi':{'available':True,'enabled':True},'bluetooth':{'available':False}}\n"
                     "with here.with_suffix('.calls').open('a') as calls: calls.write(' '.join(sys.argv[1:])+'\\n')\n"
                     "if sys.argv[1]=='set':\n"
                     "    control,value=sys.argv[2],int(sys.argv[3])\n"
                     "    if control in ('volume','brightness'): state[control]['value']=value; time.sleep(1.0)\n"
                     "    elif control=='mute': state['volume']['muted']=bool(value)\n"
                     "    else: state[control]['enabled']=bool(value)\n"
                     "    store.write_text(json.dumps(state))\n"
                     "print(json.dumps({'error':'','state':state}))\n");
        script.close();
        const auto lastCall=[&] {
            QFile calls(root.filePath("controls.calls"));
            return calls.open(QIODevice::ReadOnly) ? calls.readAll().trimmed().split('\n').last() : QByteArray{};
        };
        SystemControls controls(helper);
        QVERIFY(!controls.busy());
        QVERIFY(controls.state().isEmpty());
        controls.set("wifi",2);
        QVERIFY(!controls.error().isEmpty());
        QVERIFY(!QFileInfo::exists(root.filePath("controls.calls")));
        QQuickView view;
        view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clear=qScopeGuard([&]{view.setSource({});});
        auto *panel=view.rootObject()->findChild<QObject *>("quick-controls");
        QVERIFY(panel);
        QVERIFY(panel->setProperty("service",QVariant::fromValue(static_cast<QObject *>(&controls))));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto click=[&](const QString &name){
            if(view.grabWindow().isNull())return false;
            auto *item=visualItem(view.contentItem(),name);
            if(!item || !item->isVisible() || !item->isEnabled())return false;
            QTest::mouseClick(&view,Qt::LeftButton,{},item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
            return true;
        };
        // Strip modules are pop-up menus; the first item opens the owning panel (V922 E12).
        const auto shown=[&](const QString &name)->QQuickItem *{
            for (int attempt=0; attempt<100; ++attempt) {
                if (!view.grabWindow().isNull())
                    if (auto *item=visualItem(view.contentItem(),name); item && item->isVisible())
                        return item;
                QTest::qWait(20);
            }
            return nullptr;
        };
        const auto choose=[&](const QString &name){return shown(name) && click(name);};
        auto *strip=view.rootObject()->findChild<QObject *>("control-strip");
        QVERIFY(strip);
        // The Sound module's first item opens the Sound panel (sound_integration_tests.cpp);
        // Brightness still opens Quick Controls.
        QVERIFY(click("control-strip-monitors"));
        QVERIFY(choose("desktop-command-quick-controls"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(!visualItem(view.contentItem(),"control-strip-menu")->isVisible());
        QVERIFY(manager.shellModal());
        QTRY_VERIFY(!controls.busy());
        QCOMPARE(controls.state().value("volume").toMap().value("value").toInt(),70);
        QVERIFY(!visualItem(view.contentItem(),"quick-bluetooth")->isEnabled());
        QVERIFY(click("quick-wifi"));
        QTRY_VERIFY(!controls.busy());
        QCOMPARE(lastCall(),QByteArray("set wifi 0"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/quick-controls.png"));
        // Arrow keys commit on release; keys pressed while the helper runs must
        // not lose steps or keyboard focus when an older reply arrives.
        auto *volume=visualItem(view.contentItem(),"quick-volume");
        QVERIFY(volume);
        volume->forceActiveFocus();
        QVERIFY(volume->hasActiveFocus());
        for (int step=0; step<3; ++step) {
            QTest::keyClick(&view,Qt::Key_Right);
            QCOMPARE(volume->property("value").toInt(),71+step);
        }
        QTRY_COMPARE(lastCall(),QByteArray("set volume 73"));
        QVERIFY(controls.busy());
        QVERIFY(volume->isEnabled() && volume->hasActiveFocus());
        QTest::keyClick(&view,Qt::Key_Right);
        QTest::keyClick(&view,Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(lastCall(),QByteArray("set volume 75"),8000);
        QTRY_VERIFY_WITH_TIMEOUT(!controls.busy(),8000);
        QCOMPARE(controls.state().value("volume").toMap().value("value").toInt(),75);
        QCOMPARE(volume->property("value").toInt(),75);
        QVERIFY(volume->hasActiveFocus());
        QTest::keyClick(&view,Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        // The Sound module refreshes on opening and checks the nearest volume step.
        QVERIFY(click("control-strip-sound"));
        QTRY_COMPARE(lastCall(),QByteArray("status"));
        auto *seventyFive=shown("desktop-command-volume-75");
        QVERIFY(seventyFive);
        QVERIFY(seventyFive->property("modelData").toMap().value("checked").toBool());
        QVERIFY(!visualItem(view.contentItem(),"desktop-command-volume-50")->property("modelData").toMap().value("checked").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/control-strip-sound.png"));
        QVERIFY(choose("desktop-command-mute"));
        QTRY_COMPARE(lastCall(),QByteArray("set mute 1"));
        QTRY_VERIFY(!controls.busy());
        QVERIFY(!visualItem(view.contentItem(),"control-strip-menu")->isVisible());
        QVERIFY(click("control-strip-sound"));
        QTRY_VERIFY(!controls.busy());
        QVERIFY(shown("desktop-command-mute"));
        QVERIFY(visualItem(view.contentItem(),"desktop-command-mute")->property("modelData").toMap().value("checked").toBool());
        QVERIFY(choose("desktop-command-volume-25"));
        QTRY_COMPARE_WITH_TIMEOUT(lastCall(),QByteArray("set volume 25"),8000);
        QTRY_VERIFY_WITH_TIMEOUT(!controls.busy(),8000);
        // Wi-Fi was turned off from the panel above; the module offers the opposite state.
        QVERIFY(click("control-strip-wifi"));
        QTRY_VERIFY(!controls.busy());
        QVERIFY(shown("desktop-command-wifi"));
        QCOMPARE(visualItem(view.contentItem(),"desktop-command-wifi")->property("modelData").toMap().value("text").toString(),
                 QString("Turn Wi-Fi On"));
        QVERIFY(choose("desktop-command-wifi"));
        QTRY_COMPARE(lastCall(),QByteArray("set wifi 1"));
        QTRY_VERIFY(!controls.busy());
        QVERIFY(click("control-strip-monitors"));
        QTRY_VERIFY(!controls.busy());
        QVERIFY(choose("desktop-command-brightness-25"));
        QTRY_COMPARE_WITH_TIMEOUT(lastCall(),QByteArray("set brightness 25"),8000);
        QTRY_VERIFY_WITH_TIMEOUT(!controls.busy(),8000);
        // Bluetooth has its own service (docs/bluetooth.md, tests/bluetooth_tests.cpp). Without
        // BlueZ the module shows a disabled status line, offers no power command and sends nothing.
        const auto callsFile=[&]{QFile file(root.filePath("controls.calls"));return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};};
        const auto callsBefore=callsFile();
        QVERIFY(click("control-strip-bluetooth"));
        auto *status=shown("desktop-command-status");
        QVERIFY(status);
        QCOMPARE(status->property("modelData").toMap().value("text").toString(),QString("Bluetooth Unavailable"));
        QCOMPARE(shown("desktop-command-bluetooth")->property("modelData").toMap().value("text").toString(),QString("Open Bluetooth…"));
        QVERIFY(!visualItem(view.contentItem(),"desktop-command-bluetooth-power"));
        QTRY_VERIFY(visualItem(view.contentItem(),"control-strip-menu")->isVisible());
        QTest::qWait(300);
        QVERIFY(!controls.busy());
        QCOMPARE(callsFile(),callsBefore);
        // Clicking the desktop dismisses the menu without a command.
        QTest::mouseClick(&view,Qt::LeftButton,{},QPoint(600,300));
        QTRY_VERIFY(!visualItem(view.contentItem(),"control-strip-menu")->isVisible());
        QCOMPARE(strip->property("openModule").toString(),QString());
        QCOMPARE(callsFile(),callsBefore);
        // Malformed helper replies must not masquerade as a settings change.
        QVERIFY(script.open(QIODevice::WriteOnly|QIODevice::Truncate));
        script.write("print('not json')\n");script.close();
        controls.refresh();
        QTRY_VERIFY(!controls.busy());
        QVERIFY(!controls.error().isEmpty());
    }
    void desktopReceiverAcceptsNativeWindowEventsWithoutRequiringAnOnscreenWindow_data()
    {
        QTest::addColumn<bool>("owned");
        QTest::addColumn<bool>("option");
        QTest::newRow("ordinary URLs copy") << false << false;
        QTest::newRow("Finder URLs move") << true << false;
        QTest::newRow("Finder Option URLs copy") << true << true;
    }
    void desktopReceiverAcceptsNativeWindowEventsWithoutRequiringAnOnscreenWindow()
    {
        QFETCH(bool, owned);
        QFETCH(bool, option);
        DesktopDropReceiver receiver;
        QSignalSpy dropped(&receiver, &DesktopDropReceiver::filesDropped);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/File with spaces.txt")});
        if (owned)
            mime.setData(finderDragMime, "1");
        const auto modifiers = option ? Qt::AltModifier : Qt::NoModifier;
        QDragEnterEvent enter(QPointF(600, 400), Qt::MoveAction | Qt::CopyAction, &mime,
                              Qt::LeftButton, modifiers);
        QGuiApplication::sendEvent(&receiver, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(600, 400), Qt::MoveAction | Qt::CopyAction, &mime, Qt::LeftButton,
                        modifiers);
        QGuiApplication::sendEvent(&receiver, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.dropAction(), owned && !option ? Qt::MoveAction : Qt::CopyAction);
        QCOMPARE(dropped.count(), 1);
        QCOMPARE(dropped.first().first().toStringList(), QStringList{"/tmp/File with spaces.txt"});
        mime.setUrls({QUrl("https://example.com/not-a-file")});
        QDropEvent invalid(QPointF(600, 400), Qt::CopyAction, &mime, Qt::LeftButton, {});
        QGuiApplication::sendEvent(&receiver, &invalid);
        QVERIFY(!invalid.isAccepted());
        QCOMPARE(dropped.count(), 2);
        QVERIFY(dropped.last().first().toStringList().isEmpty());
    }
    void desktopReceiverAcceptsWindowSystemDragDispatch_data()
    {
        QTest::addColumn<QPoint>("position");
        QTest::newRow("inside the endpoint") << QPoint(1, 1);
        QTest::newRow("desktop coordinates outside the endpoint") << QPoint(240, 610);
    }
    void desktopReceiverAcceptsWindowSystemDragDispatch()
    {
        QFETCH(QPoint, position);
        DesktopDropReceiver receiver;
        receiver.show();
        QVERIFY(QTest::qWaitForWindowExposed(&receiver));
        QSignalSpy dropped(&receiver, &DesktopDropReceiver::filesDropped);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/File with spaces.txt")});
        mime.setData(finderDragMime, "transport-token");
        const auto actions = Qt::MoveAction | Qt::CopyAction;
        const auto enter = QWindowSystemInterface::handleDrag(&receiver, &mime, position, actions,
                                                              Qt::LeftButton, Qt::NoModifier);
        QVERIFY(enter.isAccepted());
        QCOMPARE(enter.acceptedAction(), Qt::MoveAction);
        const auto drop = QWindowSystemInterface::handleDrop(&receiver, &mime, position, actions,
                                                             Qt::NoButton, Qt::NoModifier);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.acceptedAction(), Qt::MoveAction);
        QCOMPARE(dropped.count(), 1);
        QCOMPARE(dropped.first().first().toStringList(), QStringList{"/tmp/File with spaces.txt"});
        QCOMPARE(dropped.first().at(1).toString(), QString("transport-token"));
    }
    void desktopReceiverReadsOwnedFileURLsWhenThePortalURLPathIsUnavailable()
    {
        PortalUnavailableFileMime mime;
        mime.payload.insert("text/uri-list", {});
        mime.payload.insert("application/vnd.portal.filetransfer", {});
        mime.payload.insert(finderDragMime, "transport-token");
        mime.payload.insert("application/x-frankenstein-local-file-urls",
                            "file:///tmp/File%20with%20spaces.txt\r\n");
        QVERIFY(mime.hasUrls());
        QVERIFY(mime.urls().isEmpty());
        mime.reads.clear();
        DesktopDropReceiver receiver;
        receiver.show();
        QVERIFY(QTest::qWaitForWindowExposed(&receiver));
        QSignalSpy dropped(&receiver, &DesktopDropReceiver::filesDropped);
        const auto actions = Qt::MoveAction | Qt::CopyAction;
        const auto enter = QWindowSystemInterface::handleDrag(
            &receiver, &mime, QPoint(1, 1), actions, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(enter.isAccepted());
        const auto drop = QWindowSystemInterface::handleDrop(&receiver, &mime, QPoint(1, 1),
                                                             actions, Qt::NoButton, Qt::NoModifier);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.acceptedAction(), Qt::MoveAction);
        QCOMPARE(dropped.count(), 1);
        QCOMPARE(dropped.first().first().toStringList(), QStringList{"/tmp/File with spaces.txt"});
        QCOMPARE(dropped.first().at(1).toString(), QString("transport-token"));
        QVERIFY(!mime.reads.contains("text/uri-list"));
        QVERIFY(!mime.reads.contains("application/vnd.portal.filetransfer"));
    }
    void appearanceChoicesPersistAndInvalidValuesPreserveTheLastSettings()
    {
        QTemporaryDir root;
        AppearanceSettings settings(root.path());
        QCOMPARE(settings.shadeDuration(), 240);
        QVERIFY(settings.controlStripVisible());
        QVERIFY(settings.apply(600, false));
        QVERIFY(settings.setControlStripCollapsed(true));
        QVERIFY(settings.setControlStripPosition(70, 440));
        AppearanceSettings restored(root.path());
        QCOMPARE(restored.shadeDuration(), 600);
        QVERIFY(!restored.controlStripVisible());
        QVERIFY(restored.controlStripCollapsed());
        QCOMPARE(restored.controlStripX(), 70);
        QCOMPARE(restored.controlStripY(), 440);
        QSignalSpy errors(&settings, &AppearanceSettings::errorOccurred);
        QVERIFY(!settings.apply(-1, true));
        QVERIFY(!settings.apply(1201, true));
        QVERIFY(!settings.setControlStripPosition(-1, 30));
        QCOMPARE(errors.count(), 3);
        AppearanceSettings unchanged(root.path());
        QCOMPARE(unchanged.shadeDuration(), 600);
        QVERIFY(!unchanged.controlStripVisible());
        QFile file(root.path() + "/appearance.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\"version\":1,\"shadeDuration\":999999}");
        file.close();
        AppearanceSettings invalid(root.path());
        QCOMPARE(invalid.shadeDuration(), 240);
        QCOMPARE(settings.fontFamily(), classicInterfaceFontFamily());
    }
    void screenshotCropSavesTheActualRenderedPixels()
    {
        QTemporaryDir root;
        QQuickView view;
        view.resize(200, 100);
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nRectangle {width:200;height:100;color:'red';"
                          "Rectangle{x:100;width:100;height:100;color:'blue'}}", QUrl());
        QScopedPointer<QQuickItem> item(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(item, qPrintable(component.errorString()));
        item->setParentItem(view.contentItem());
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        ScreenshotService tool(&view);
        QVERIFY(tool.capture());
        QCOMPARE(tool.imageSize(), QSize(200,100));
        QCOMPARE(tool.selection(), QRect(0,0,200,100));
        QVERIFY(!tool.preview().isEmpty());
        QVERIFY(tool.select(100,0,100,100));
        const auto path = root.path() + "/Screenshots/Cropped.png";
        QVERIFY(tool.save(path));
        QImage saved(path);
        QCOMPARE(saved.size(), QSize(100,100));
        QCOMPARE(saved.pixelColor(30,30), QColor(Qt::blue));
        QCOMPARE(QFileInfo(path).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther |
                  QFileDevice::WriteGroup | QFileDevice::WriteOther), QFileDevice::Permissions{});
        tool.discard();
        QVERIFY(tool.preview().isEmpty());
        QVERIFY(tool.imageSize().isEmpty());
    }
    void screenshotErrorsAndCancellationPreserveFilesAndSelection()
    {
        QTemporaryDir root;
        QQuickView view;
        view.resize(100,100);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        ScreenshotService tool(&view);
        QVERIFY(tool.capture());
        const auto previous = tool.selection();
        QVERIFY(!tool.select(-1,0,50,50));
        QVERIFY(!tool.select(0,0,0,20));
        QVERIFY(!tool.select(50,50,100,100));
        QCOMPARE(tool.selection(), previous);
        QFile original(root.path()+"/Keep.png");
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("preserve these bytes");
        original.close();
        QVERIFY(!tool.save(original.fileName()));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("preserve these bytes"));
        QVERIFY(!tool.save("relative.png"));
        QVERIFY(!tool.save(root.path()+"/Wrong.jpg"));
        tool.discard();
        QVERIFY(!tool.save(root.path()+"/Cancelled.png"));
        QVERIFY(!QFileInfo::exists(root.path()+"/Cancelled.png"));
    }
    void keepAwakePreferenceRetainsItsStateWhenSavingFailsAndAcceptsOldProfiles()
    {
        QTemporaryDir root;
        AppearanceSettings settings(root.path());
        QVERIFY(settings.apply(600, false));
        QVERIFY(settings.setKeepScreenAwake(true));
        QFile file(root.path()+"/appearance.json"); QVERIFY(file.open(QIODevice::ReadOnly));
        auto object=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        QVERIFY(QFile::remove(file.fileName()));
        QVERIFY(QDir().mkdir(file.fileName())); // Atomic save cannot replace a directory.
        QSignalSpy changed(&settings,&AppearanceSettings::changed);
        QSignalSpy errors(&settings,&AppearanceSettings::errorOccurred);
        QVERIFY(!settings.setKeepScreenAwake(false));
        QVERIFY(settings.keepScreenAwake());
        QCOMPARE(changed.count(),0); QCOMPARE(errors.count(),1);
        QVERIFY(QDir().rmdir(file.fileName()));
        object["keepScreenAwake"]="true";
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(object).toJson());file.close();
        AppearanceSettings malformed(root.path());
        QVERIFY(!malformed.keepScreenAwake()); QCOMPARE(malformed.shadeDuration(),600);
        object.remove("keepScreenAwake");
        QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));file.write(QJsonDocument(object).toJson());file.close();
        AppearanceSettings older(root.path());
        QVERIFY(!older.keepScreenAwake()); QVERIFY(!older.controlStripVisible());
    }
    void desktopPicturesPersistWithoutOverwritingOtherAppearanceChoices()
    {
        QTemporaryDir root;
        const auto picture = root.path() + "/Picture with spaces.png";
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(QColor("#cf3148"));
        QVERIFY(image.save(picture));
        AppearanceSettings settings(root.path() + "/profile");
        QVERIFY(settings.apply(600, false));
        QVERIFY(settings.setControlStripPosition(92, 303));
        QVERIFY(settings.applyDesktopPicture(picture, "fit"));
        QCOMPARE(settings.wallpaper(), QUrl::fromLocalFile(picture));
        QVERIFY(settings.setControlStripCollapsed(true));
        AppearanceSettings restored(root.path() + "/profile");
        QCOMPARE(restored.wallpaperPath(), picture);
        QCOMPARE(restored.wallpaperLayout(), QString("fit"));
        QCOMPARE(restored.shadeDuration(), 600);
        QCOMPARE(restored.controlStripX(), 92);
        QCOMPARE(restored.controlStripY(), 303);
        QVERIFY(!restored.controlStripVisible());
        QVERIFY(restored.controlStripCollapsed());
        QVERIFY(settings.applyDesktopPicture("", "center"));
        AppearanceSettings solid(root.path() + "/profile");
        QVERIFY(solid.wallpaper().isEmpty());
        QVERIFY(solid.wallpaperPath().isEmpty());
        QCOMPARE(solid.wallpaperLayout(), QString("center"));
        QVERIFY(settings.applyDesktopPicture(picture, "tile"));
        QVERIFY(QFile::remove(picture));
        AppearanceSettings missing(root.path() + "/profile");
        QVERIFY(missing.wallpaper().isEmpty());
        QCOMPARE(missing.wallpaperPath(), picture);
        QCOMPARE(missing.wallpaperLayout(), QString("tile"));
        QCOMPARE(missing.shadeDuration(), 600);
    }
    void aNewProfileHasThePlainDesktopAndBrowsesPictures()
    {
        // Public build: nothing personal by default (this was the owner's ~/wallpaper/another1.png).
        QTemporaryDir root;
        AppearanceSettings fresh(root.path());
        QVERIFY(fresh.defaultWallpaperPath().isEmpty());
        QVERIFY(fresh.wallpaperPath().isEmpty());
        QVERIFY(QFileInfo(fresh.picturesFolder()).isDir());
        QVERIFY(!fresh.picturesFolder().endsWith("/wallpaper"));
    }
    void earlierAppearanceProfilesKeepTheirPreferencesWhenAddingPictures()
    {
        QTemporaryDir root;
        QFile profile(root.path() + "/appearance.json");
        QVERIFY(profile.open(QIODevice::WriteOnly));
        profile.write("{\"version\":1,\"shadeDuration\":370,\"stripVisible\":false,"
                      "\"stripCollapsed\":true,\"stripX\":15,\"stripY\":60}");
        profile.close();
        AppearanceSettings old(root.path());
        QCOMPARE(old.shadeDuration(), 370);
        QVERIFY(!old.controlStripVisible());
        QVERIFY(old.controlStripCollapsed());
        QCOMPARE(old.wallpaperPath(), old.defaultWallpaperPath());
        QCOMPARE(old.wallpaperLayout(), QString("fill"));
        QVERIFY(old.applyDesktopPicture("", "tile"));
        AppearanceSettings upgraded(root.path());
        QCOMPARE(upgraded.shadeDuration(), 370);
        QVERIFY(!upgraded.controlStripVisible());
        QCOMPARE(upgraded.controlStripX(), 15);
        QCOMPARE(upgraded.controlStripY(), 60);
        QVERIFY(upgraded.wallpaperPath().isEmpty());
    }
    void invalidOrUnsaveableDesktopPicturesPreserveThePreviousSelection()
    {
        QTemporaryDir root;
        const auto picture = root.path() + "/Valid.png";
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(picture));
        const auto profile = root.path() + "/profile";
        AppearanceSettings settings(profile);
        QVERIFY(settings.applyDesktopPicture(picture, "fill"));
        QFile previous(profile + "/appearance.json");
        QVERIFY(previous.open(QIODevice::ReadOnly));
        const auto before = previous.readAll();
        previous.close();
        QSignalSpy errors(&settings, &AppearanceSettings::errorOccurred);
        QVERIFY(!settings.applyDesktopPicture(picture, "unknown"));
        QVERIFY(!settings.applyDesktopPicture("relative.png", "fill"));
        QVERIFY(!settings.applyDesktopPicture(root.path(), "fill"));
        QVERIFY(!settings.applyDesktopPicture("https://example.org/image.png", "fill"));
        QFile broken(root.path() + "/Broken.png");
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("this is not a picture");
        broken.close();
        QVERIFY(!settings.applyDesktopPicture(broken.fileName(), "fill"));
        QFile original(picture);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto header = original.read(33); // Valid dimensions, missing image data.
        original.close();
        QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(broken.write(header), header.size());
        broken.close();
        QVERIFY(!settings.applyDesktopPicture(broken.fileName(), "fill"));
        QCOMPARE(errors.count(), 6);
        QVERIFY(previous.open(QIODevice::ReadOnly));
        QCOMPARE(previous.readAll(), before);
        previous.close();
        QCOMPARE(settings.wallpaperPath(), picture);
        QVERIFY(QFile::remove(profile + "/appearance.json"));
        QVERIFY(QDir().mkdir(profile + "/appearance.json"));
        QVERIFY(!settings.applyDesktopPicture("", "fit"));
        QCOMPARE(settings.wallpaperPath(), picture);
        QCOMPARE(settings.wallpaperLayout(), QString("fill"));
    }
    void pictureBrowserFiltersFilesAndBoundsDirectoryEnumeration()
    {
        QTemporaryDir root;
        AppearanceSettings settings(root.path() + "/profile");
        QImage image(8, 4, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(root.path() + "/B.png"));
        QVERIFY(image.save(root.path() + "/a.jpg"));
        QVERIFY(QDir().mkdir(root.path() + "/folder.png"));
        QFile text(root.path() + "/not-an-image.png");
        QVERIFY(text.open(QIODevice::WriteOnly));
        text.write("text");
        text.close();
        const auto entries = settings.listDesktopPictures(root.path()).value("files").toList();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.at(0).toMap().value("name").toString(), QString("a.jpg"));
        QCOMPARE(entries.at(1).toMap().value("name").toString(), QString("B.png"));
        QVERIFY(settings.listDesktopPictures(root.path() + "/missing").value("files").toList().isEmpty());
        for (int i = 0; i < 130; ++i)
            QVERIFY(image.save(root.path() + QString("/bounded-%1.png").arg(i)));
        const auto limited = settings.listDesktopPictures(root.path());
        QCOMPARE(limited.value("files").toList().size(), 128);
        QVERIFY(limited.value("notice").toString().contains("limited"));
    }
    void desktopPictureDialogOffersTheUsersCollections()
    {
        // Collections in the user's own assets (Mac OS 9's Desktop Pictures, imported) are one
        // click away; symlinked folders and relative roots are not collections.
        QTemporaryDir root;
        const auto previous = qgetenv("XDG_DATA_HOME");
        const auto restore = qScopeGuard([&] { qputenv("XDG_DATA_HOME", previous); });
        qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
        const auto pictures = root.filePath("data/platinum-reality/assets/desktop-pictures");
        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(QColor("#3a8fd0"));
        for (const auto *collection : {"Photos", "Ensemble Pictures"})
        {
            QVERIFY(QDir().mkpath(pictures + "/" + collection));
            QVERIFY(image.save(pictures + "/" + collection + "/" + collection + " One.jpg", "JPG"));
        }
        QVERIFY(QFile::link(pictures + "/Photos", pictures + "/Linked"));
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.filePath("ws"), root.filePath("profile"), {});
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings);
        QStringList names;
        for (const auto &entry : settings->pictureCollections(pictures))
            names << entry.toMap().value("name").toString();
        QCOMPARE(names, QStringList({"Ensemble Pictures", "Photos"}));
        QVERIFY(settings->pictureCollections("relative/desktop-pictures").isEmpty());
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *panel = view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(panel && QMetaObject::invokeMethod(panel, "edit"));
        QTRY_VERIFY(panel->property("opened").toBool());
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        QTRY_VERIFY(item("picture-collection-Photos") && item("picture-collection-Photos")->isVisible());
        QVERIFY(item("picture-collection-pictures"));
        auto *photos = item("picture-collection-Photos");
        photos->parentItem()->ensurePolished();
        QTest::mouseClick(&view, Qt::LeftButton, {}, photos->mapToScene(QPointF(photos->width() / 2, photos->height() / 2)).toPoint());
        QTRY_COMPARE(item("picture-folder") ? item("picture-folder")->property("text").toString() : QString(), pictures + "/Photos");
        const auto rows = panel->property("pictureRows").toList();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.at(0).toMap().value("name").toString(), QString("Photos One.jpg"));
    }

    void desktopPictureDialogPreviewsCancelsAppliesAndRestores()
    {
        QTemporaryDir root;
        const auto picture = root.path() + "/Chosen picture.png";
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(QColor("#cf3148"));
        QVERIFY(image.save(picture));
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings && settings->applyDesktopPicture("", "fill"));
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto failureCapture = qScopeGuard([&] {
            if (QTest::currentTestFailed())
                view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/picture-selection-failure.png");
        });
        auto click = [&](const QString &name) {
            // Interact with rendered geometry, including the dialog's deferred
            // Column layout, rather than an item that has only been created.
            if (view.grabWindow().isNull()) return false;
            auto *item = visualItem(view.contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled()) return false;
            item->parentItem()->ensurePolished();
            QTest::mouseClick(&view, Qt::LeftButton, {},
                             item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
            return true;
        };
        auto open = [&] {
            // Apple menu > Control Panels > Desktop Picture… (Mac OS 9 layout).
            QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(25, 13));
            if (!click("desktop-command-control-panels")) return false;
            auto *entry = visualItem(view.contentItem(), "desktop-command-desktop-picture");
            if (!entry) return false;
            entry->parentItem()->ensurePolished();
            return click("desktop-command-desktop-picture");
        };
        QVERIFY(open());
        auto *panel = view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(manager.shellModal());
        auto *folder = visualItem(view.contentItem(), "picture-folder");
        QVERIFY(folder);
        folder->setProperty("text", root.path());
        QVERIFY(click("picture-read-folder"));
        QTRY_VERIFY(visualItem(view.contentItem(), "picture-row-0"));
        QTRY_VERIFY(visualItem(view.contentItem(), "picture-row-0")->width() > 0);
        // A delegate can exist before the enclosing Column positions its Row.
        // Wait for the list to be below the folder field before hit-testing it.
        QTRY_VERIFY(visualItem(view.contentItem(), "picture-row-0")->mapToScene(QPointF()).y() >=
                    folder->mapToScene(QPointF(0, folder->height())).y());
        QVERIFY(click("picture-row-0"));
        QTRY_COMPARE(panel->property("draftPath").toString(), picture);
        auto *preview = visualItem(view.contentItem(), "picture-preview");
        QVERIFY(preview);
        QTRY_COMPARE(preview->property("status").toInt(), 1);
        QVERIFY(settings->wallpaperPath().isEmpty());
        QVERIFY(click("picture-layout-fit"));
        const auto previewPixel = [&] {
            const auto center = preview->mapToScene(QPointF(preview->width()/2, preview->height()/2));
            return view.grabWindow().pixelColor(center.toPoint());
        };
        QTRY_COMPARE(previewPixel(), QColor("#cf3148"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/desktop-picture.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(settings->wallpaperPath().isEmpty());
        QVERIFY(open());
        QTRY_VERIFY(panel->property("opened").toBool());
        panel->setProperty("draftPath", picture);
        QVERIFY(click("picture-layout-fit"));
        QVERIFY(click("desktop-picture-dialog-accept"));
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(settings->wallpaperPath(), picture);
        QCOMPARE(settings->wallpaperLayout(), QString("fit"));
        auto *desktopImage = visualItem(view.contentItem(), "desktop-wallpaper");
        QVERIFY(desktopImage);
        QTRY_COMPARE(desktopImage->property("status").toInt(), 1);
        QTRY_COMPARE(view.grabWindow().pixelColor(400, 60), QColor("#708b97"));
        QTRY_COMPARE(view.grabWindow().pixelColor(400, 180), QColor("#cf3148"));
        AppearanceSettings restored(root.path() + "/profile");
        QCOMPARE(restored.wallpaperPath(), picture);
        QCOMPARE(restored.wallpaperLayout(), QString("fit"));
        QVERIFY(open());
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(panel->property("draftPath").toString(), picture);
        QVERIFY(click("picture-solid"));
        QVERIFY(click("desktop-picture-dialog-accept"));
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(settings->wallpaper().isEmpty());
    }
    void windowRollHasIntermediateFramesReversesAndDoesNotResizeTheContent()
    {
        QQuickView view;
        view.resize(420, 380);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQmlComponent component(view.engine());
        component.setData("\n"
                          "            import QtQuick\n"
                          "            import \"qrc:/desktop/src/desktop\"\n"
                          "            WindowRoll {\n"
                          "                width:360;contentHeight:300;animationDuration:600\n"
                          "                Rectangle "
                          "{anchors.fill:parent;color:\"#ccc\";border.color:\"black\"}\n"
                          "                Rectangle "
                          "{x:3;y:23;width:354;height:300;color:\"white\";Text{x:14;y:20;text:"
                          "\"This client remains full size.\"}}\n"
                          "                Rectangle "
                          "{x:2;y:2;width:356;height:19;color:\"#bbb\";Text{anchors.centerIn:"
                          "parent;text:\"WindowShade\"}}\n"
                          "            }\n"
                          "        \n",
                          QUrl());
        QScopedPointer<QQuickItem> roll(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(roll, qPrintable(component.errorString()));
        roll->setParentItem(view.contentItem());
        QCOMPARE(roll->height(), 327);
        QVERIFY(!roll->property("animating").toBool());
        roll->setProperty("collapsed", true);
        QTRY_VERIFY(roll->property("animating").toBool());
        QTRY_VERIFY(roll->height() > 23 && roll->height() < 327);
        QVERIFY(roll->clip());
        QCOMPARE(roll->property("contentHeight").toInt(), 300);
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/window-roll.png"));
        const auto intermediate = roll->height();
        roll->setProperty("collapsed", false); // Reverse without jumping to either endpoint.
        QCOMPARE(roll->height(), intermediate);
        QTRY_VERIFY(!roll->property("animating").toBool());
        QCOMPARE(roll->height(), 327);
        QVERIFY(!roll->clip());
        roll->setProperty("animationDuration", 0);
        roll->setProperty("collapsed", true);
        QTRY_COMPARE(roll->height(), 23);
        QVERIFY(!roll->property("animating").toBool());
        roll->setProperty("collapsed", false);
        QTRY_COMPARE(roll->height(), 327);
        roll->setProperty("contentHeight", 330);
        QCOMPARE(roll->height(), 357);
        QVERIFY(!roll->property("animating").toBool());
        roll->setProperty("serverDecorated", false);
        roll->setProperty("collapsed", true);
        QCOMPARE(roll->height(), 330);
        QVERIFY(!roll->clip());
    }
    void monitorsPanelSavesUseAllDisplaysForTheNextLogin()
    {
        // psn_tch (2026-10-21): two monitors, opt-in. The choice is saved in the desktop profile for
        // the session supervisor to read at the next login; Cancel keeps the saved choice.
        QTemporaryDir root;
        const auto profile = root.path() + "/profile";
        const auto saved = [&] {
            QFile file(profile + "/displays.json");
            return file.open(QIODevice::ReadOnly)
                       ? QJsonDocument::fromJson(file.readAll()).object().value("mode").toString() : QString();
        };
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), profile, {});
        QVERIFY(!manager.useAllDisplays()); // Off unless chosen.
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *panel = view.rootObject()->findChild<QObject *>("monitors-panel");
        QVERIFY(panel);
        const auto item = [&](const QString &name) {
            view.grabWindow();
            return visualItem(view.contentItem(), name);
        };
        const auto click = [&](QQuickItem *target) {
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
        };
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        QQuickItem *useAll = nullptr, *note = nullptr, *hint = nullptr;
        QTRY_VERIFY((useAll = item("monitors-use-all")) && (note = item("monitors-use-all-note")) &&
                    (hint = item("monitors-unavailable")));
        QCOMPARE(hint->property("text").toString(),
                 QString("To use more than one display, turn on “Use all displays” and log in again."));
        QVERIFY(!useAll->property("checked").toBool());
        click(useAll);
        QTRY_VERIFY(useAll->property("checked").toBool());
        QVERIFY(note->property("text").toString().startsWith("“Use all displays” changes the next time you log in."));
        QVERIFY(saved().isEmpty()); // Nothing is saved before OK.
        click(item("monitors-panel-accept"));
        QTRY_VERIFY(!panel->property("visible").toBool());
        QCOMPARE(saved(), QString("extend"));
        QVERIFY(manager.useAllDisplays());
        // A later session reads the choice back.
        QQuickView other;
        NativeManager reloaded(nullptr, &other, root.path(), profile, {});
        QVERIFY(reloaded.useAllDisplays());
        // Cancel keeps it; OK with the box cleared saves one display again.
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_VERIFY(item("monitors-use-all")->property("checked").toBool());
        click(item("monitors-use-all"));
        click(item("monitors-panel-cancel"));
        QTRY_VERIFY(!panel->property("visible").toBool());
        QCOMPARE(saved(), QString("extend"));
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        click(item("monitors-use-all"));
        click(item("monitors-panel-accept"));
        QTRY_VERIFY(!panel->property("visible").toBool());
        QCOMPARE(saved(), QString("single"));
        QVERIFY(!manager.useAllDisplays());
    }
    void dialogKeysFollowMacOS9()
    {
        // Finishing School FS-D01/FS-E01: Return and keypad Enter press the default (ringed)
        // button once; Escape and Command-period (Super, psn_tch's Command key) cancel and discard
        // the draft; a destructive confirmation defaults to Cancel; only the dialog holding focus
        // answers; a field that handles Return itself keeps it.
        QTemporaryDir root;
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *panel = view.rootObject()->findChild<QObject *>("monitors-panel");
        QVERIFY(panel);
        QSignalSpy accepted(panel, SIGNAL(accepted())), rejected(panel, SIGNAL(rejected()));
        const auto edit = [&](bool useAll)
        {
            QMetaObject::invokeMethod(panel, "edit");
            QTRY_VERIFY(panel->property("opened").toBool());
            QTRY_VERIFY(panel->property("holdsFocus").toBool());
            panel->setProperty("draftUseAll", useAll);
        };
        const auto closed = [&](QObject *dialog) { return QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 5000); };
        edit(true);
        QTest::keyClick(&view, Qt::Key_Return);
        QVERIFY(closed(panel));
        QCOMPARE(accepted.size(), 1);
        QVERIFY(manager.useAllDisplays());
        edit(false);
        QTest::keyClick(&view, Qt::Key_Period, Qt::MetaModifier);
        QVERIFY(closed(panel));
        QCOMPARE(rejected.size(), 1);
        QVERIFY(manager.useAllDisplays());
        edit(false);
        QTest::keyClick(&view, Qt::Key_Escape);
        QVERIFY(closed(panel));
        QVERIFY(manager.useAllDisplays());
        QCOMPARE(accepted.size(), 1);
        edit(false);
        QTest::keyClick(&view, Qt::Key_Enter, Qt::KeypadModifier);
        QVERIFY(closed(panel));
        QCOMPARE(accepted.size(), 2);
        QVERIFY(!manager.useAllDisplays());
        // A message over the panel takes Return first; then the panel has it again.
        edit(true);
        auto *message = view.rootObject()->findChild<QObject *>("desktop-message-dialog");
        QVERIFY(message);
        QSignalSpy messageAccepted(message, SIGNAL(accepted()));
        QMetaObject::invokeMethod(message, "open");
        QTRY_VERIFY(message->property("opened").toBool());
        QTRY_VERIFY(message->property("holdsFocus").toBool() && !panel->property("holdsFocus").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QVERIFY(closed(message));
        QCOMPARE(messageAccepted.size(), 1);
        QVERIFY(panel->property("visible").toBool());
        QCOMPARE(accepted.size(), 2);
        QTRY_VERIFY(panel->property("holdsFocus").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QVERIFY(closed(panel));
        QCOMPARE(accepted.size(), 3);
        QVERIFY(manager.useAllDisplays());
        // A destructive confirmation: Return is Cancel.
        auto *forget = view.rootObject()->findChild<QObject *>("bluetooth-forget-dialog");
        QVERIFY(forget);
        QSignalSpy forgetAccepted(forget, SIGNAL(accepted())), forgetRejected(forget, SIGNAL(rejected()));
        QMetaObject::invokeMethod(forget, "open");
        QTRY_VERIFY(forget->property("opened").toBool() && forget->property("holdsFocus").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QVERIFY(closed(forget));
        QCOMPARE(forgetAccepted.size(), 0);
        QCOMPARE(forgetRejected.size(), 1);
        // Desktop Picture's folder field shows the folder on Return instead of pressing OK; the
        // picture field beside it presses OK.
        auto *pictures = view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(pictures);
        QSignalSpy picturesAccepted(pictures, SIGNAL(accepted()));
        QMetaObject::invokeMethod(pictures, "edit");
        QTRY_VERIFY(pictures->property("opened").toBool());
        view.grabWindow();
        auto *folder = visualItem(view.contentItem(), "picture-folder");
        QVERIFY(folder);
        folder->forceActiveFocus();
        QTRY_VERIFY(folder->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::qWait(300);
        QVERIFY(pictures->property("visible").toBool());
        QCOMPARE(picturesAccepted.size(), 0);
        QTest::keyClick(&view, Qt::Key_Period, Qt::MetaModifier);
        QVERIFY(closed(pictures));
        QCOMPARE(picturesAccepted.size(), 0);
    }
    void getInfoOpensModelessWindowsBoundToTheirItems()
    {
        // Finishing School FS-G01: File ▸ Get Info (Super+I) opens a modeless, read-only Info
        // window for the selected item. It stays bound to that item, opens once per item, and
        // closes with its close box or Escape. Nothing is written.
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        {
            QFile notes(items->directory() + "/Notes.txt");
            QVERIFY(notes.open(QIODevice::WriteOnly) && notes.write("hello, world") == 12);
        }
        QVERIFY(QDir(items->directory()).mkpath("Photos/Summer"));
        items->refresh();
        const auto key = [&](const QString &name)
        {
            for (int row = 0; row < items->rowCount(); ++row)
                if (items->data(items->index(row), DesktopItems::NameRole).toString() == name)
                    return items->data(items->index(row), DesktopItems::KeyRole).toString();
            return QString();
        };
        const auto listing = [&] { return QDir(items->directory()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); };
        const auto before = listing();
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        const auto text = [&](const QString &name) { auto *found = item(name); return found ? found->property("text").toString() : QString(); };
        const auto notes = key("Notes.txt"), photos = key("Photos");
        QVERIFY(!notes.isEmpty() && !photos.isEmpty());
        items->select(notes);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand", Q_ARG(QVariant, "get-info"), Q_ARG(QVariant, 0)));
        QTRY_VERIFY(item("info-" + notes));
        QCOMPARE(text("info-name-" + notes), QString("Notes.txt"));
        QCOMPARE(text("info-value-" + notes + "-Kind"), QString("Plain text document"));
        QCOMPARE(text("info-value-" + notes + "-Size"), QString("12 bytes (12 bytes)"));
        QCOMPARE(text("info-value-" + notes + "-Where"), items->directory());
        QVERIFY(!manager.shellModal()); // Modeless: the desktop stays usable.
        // Bound to its item: a new selection does not retarget it.
        items->clearSelection();
        items->select(photos);
        QCOMPARE(text("info-name-" + notes), QString("Notes.txt"));
        QTest::keyClick(&view, Qt::Key_I, Qt::MetaModifier);
        QTRY_VERIFY(item("info-" + photos));
        QCOMPARE(text("info-value-" + photos + "-Size"), QString("Contains 1 item"));
        QTest::keyClick(&view, Qt::Key_I, Qt::MetaModifier);
        QTest::qWait(100);
        int open = 0;
        for (auto *candidate : view.rootObject()->findChildren<QQuickItem *>())
            open += candidate->objectName() == "info-" + photos;
        QCOMPARE(open, 1);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/get-info.png"));
        // Escape closes the focused window; the close box closes the other.
        QTRY_VERIFY(item("info-" + photos)->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!item("info-" + photos));
        auto *close = item("info-close-" + notes);
        QVERIFY(close);
        QTest::mouseClick(&view, Qt::LeftButton, {}, close->mapToScene(QPointF(6, 6)).toPoint());
        QTRY_VERIFY(!item("info-" + notes));
        QCOMPARE(listing(), before);
    }
    void interfaceSoundsPlayTheUsersImportedTrackOnlyWhenChosen()
    {
        // The Appearance sound track (F17): theme sounds come only from the user's own import
        // (sounds/platinum/manifest.json), never from the build. Tags map through the manifest;
        // qualified parts ("wmov moving") need their full text; files must be <id>.wav valid
        // WAVs in the folder. The alert volume applies; "None" persists. A fake player records.
        QTemporaryDir root;
        const auto profile = root.filePath("profile"), assets = root.filePath("assets");
        const auto folder = assets + "/sounds/platinum";
        QVERIFY(QDir().mkpath(folder));
        const auto wave = [](const QString &path)
        {
            QFile file(path);
            QByteArray header("RIFF");
            header += QByteArray::fromHex("24000000") + "WAVEfmt " + QByteArray::fromHex("10000000010001002256000044ac00000200100064617461")
                      + QByteArray::fromHex("00000000");
            return file.open(QIODevice::WriteOnly) && file.write(header) == header.size();
        };
        const auto calls = root.filePath("player.calls"), player = root.filePath("player");
        {
            QFile script(player);
            QVERIFY(script.open(QIODevice::WriteOnly));
            script.write(("#!/bin/sh\nprintf '%s\\n' \"$*\" >> '" + calls + "'\n").toUtf8());
        }
        QVERIFY(QFile::setPermissions(player, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto recorded = [&]
        {
            QFile file(calls);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed().split('\n', Qt::SkipEmptyParts) : QStringList();
        };
        int volume = 60;
        {
            InterfaceSounds none(profile, assets, player, [&] { return volume; });
            QVERIFY(!none.available());
            QSignalSpy played(&none, &InterfaceSounds::played);
            none.play("mnuo");
            QCOMPARE(played.size(), 0);
        }
        QVERIFY(wave(folder + "/515.wav") && wave(folder + "/331.wav") && wave(folder + "/504.wav") && wave(assets + "/x.wav"));
        {
            QFile stub(folder + "/10.wav");
            QVERIFY(stub.open(QIODevice::WriteOnly) && stub.write("not a sound!!\n") == 14);
            QFile manifest(folder + "/manifest.json");
            QVERIFY(manifest.open(QIODevice::WriteOnly));
            manifest.write(R"({"515": {"file": "515.wav", "tags": "mnuo"}, "331": {"file": "331.wav", "tags": "chke/lare"},
                               "504": {"file": "504.wav", "tags": "wmov moving"}, "9": {"file": "../x.wav", "tags": "evil"},
                               "10": {"file": "10.wav", "tags": "brkn"}})");
        }
        {
            InterfaceSounds sounds(profile, assets, player, [&] { return volume; });
            QVERIFY(sounds.available() && sounds.enabled());
            QSignalSpy played(&sounds, &InterfaceSounds::played);
            sounds.play("mnuo");
            QTRY_COMPARE(recorded(), QStringList({"--volume 0.60 " + folder + "/515.wav"}));
            sounds.play("lare");
            QTRY_COMPARE(recorded().size(), 2);
            QCOMPARE(recorded().last(), "--volume 0.60 " + folder + "/331.wav");
            for (const auto *tag : {"wmov", "evil", "brkn", "nope"})
                sounds.play(tag);
            sounds.play("wmov moving");
            QTRY_COMPARE(recorded().size(), 3);
            QCOMPARE(recorded().last(), "--volume 0.60 " + folder + "/504.wav");
            QCOMPARE(played.size(), 3);
            volume = 0;
            sounds.play("mnuo");
            volume = 25;
            sounds.setEnabled(false);
            sounds.play("mnuo");
            QTest::qWait(200);
            QCOMPARE(played.size(), 3);
            QCOMPARE(recorded().size(), 3);
        }
        {
            InterfaceSounds reopened(profile, assets, player, [&] { return volume; });
            QVERIFY(reopened.available() && !reopened.enabled());
            reopened.setEnabled(true);
            reopened.play("mnuo");
            QTRY_COMPARE(recorded().size(), 4);
            QCOMPARE(recorded().last(), "--volume 0.25 " + folder + "/515.wav");
            InterfaceSounds silent(profile, assets, QString(), [&] { return volume; });
            QSignalSpy played(&silent, &InterfaceSounds::played);
            silent.play("mnuo"); // Reported (for desktop tests), never heard.
            QCOMPARE(played.size(), 1);
            QTest::qWait(200);
            QCOMPARE(recorded().size(), 4);
        }
    }

    void desktopPlaysTheImportedSoundTrackAndDrawsPersonalIcons()
    {
        // In the desktop, with a user's imported assets in their data home: menus play mnuo and
        // mnuc, the Trash draws the personal icon instead of the original drawing, and the
        // Appearance Sound tab's track choice persists. No player: played() reports, nothing sounds.
        QTemporaryDir root;
        const auto previous = qgetenv("XDG_DATA_HOME");
        const auto restore = qScopeGuard([&] { qputenv("XDG_DATA_HOME", previous); });
        qputenv("XDG_DATA_HOME", root.filePath("data").toUtf8());
        const auto assets = root.filePath("data/platinum-reality/assets");
        QVERIFY(QDir().mkpath(assets + "/sounds/platinum") && QDir().mkpath(assets + "/icons"));
        for (const auto *name : {"515", "516", "330", "333", "414", "415"})
        {
            QFile file(assets + "/sounds/platinum/" + name + ".wav");
            QByteArray header("RIFF");
            header += QByteArray::fromHex("24000000") + "WAVEfmt " + QByteArray::fromHex("10000000010001002256000044ac00000200100064617461")
                      + QByteArray::fromHex("00000000");
            QVERIFY(file.open(QIODevice::WriteOnly) && file.write(header) == header.size());
        }
        {
            QFile manifest(assets + "/sounds/platinum/manifest.json");
            QVERIFY(manifest.open(QIODevice::WriteOnly));
            manifest.write(R"({"515": {"file": "515.wav", "tags": "mnuo"}, "516": {"file": "516.wav", "tags": "mnuc"},
                               "330": {"file": "330.wav", "tags": "chkp"}, "333": {"file": "333.wav", "tags": "chkr"},
                               "414": {"file": "414.wav", "tags": "tabp"}, "415": {"file": "415.wav", "tags": "tabr"}})");
        }
        QImage red(32, 32, QImage::Format_ARGB32);
        red.fill(QColor(255, 0, 0));
        QVERIFY(red.save(assets + "/icons/trash.png"));
        QImage blue(32, 32, QImage::Format_ARGB32);
        blue.fill(QColor(0, 0, 255));
        QVERIFY(blue.save(assets + "/icons/trash-full.png"));
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("desktop-profile"), {});
        QCOMPARE(manager.personalAssets(), assets);
        auto *sounds = qobject_cast<InterfaceSounds *>(manager.interfaceSounds());
        QVERIFY(sounds && sounds->available() && sounds->enabled());
        QSignalSpy played(sounds, &InterfaceSounds::played);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        QTRY_VERIFY(item("desktop-label-Trash"));
        const auto label = item("desktop-label-Trash")->mapToScene(QPointF(item("desktop-label-Trash")->width() / 2, -20));
        QTRY_COMPARE(view.grabWindow().pixelColor(label.toPoint()), QColor(255, 0, 0));
        {
            // Something in Trash (here written directly, as the Finder's trash would) fills it.
            QFile trashed(root.filePath("desktop-profile/data/Trash/files/old note.txt"));
            QVERIFY(trashed.open(QIODevice::WriteOnly) && trashed.write("x") == 1);
        }
        QTRY_COMPARE(view.grabWindow().pixelColor(label.toPoint()), QColor(0, 0, 255));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "showMenu", Q_ARG(QVariant, 0)));
        QTRY_COMPARE(played.size(), 1);
        QCOMPARE(played.last().first().toString(), QString("mnuo"));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "closeMenu", Q_ARG(QVariant, true), Q_ARG(QVariant, QString("mnuc"))));
        QTRY_COMPARE(played.size(), 2);
        QCOMPARE(played.last().first().toString(), QString("mnuc"));
        auto *appearance = view.rootObject()->findChild<QObject *>("appearance-dialog");
        QVERIFY(appearance);
        QVERIFY(QMetaObject::invokeMethod(appearance, "edit", Q_ARG(QVariant, QString("Sound"))));
        QTRY_VERIFY(appearance->property("opened").toBool());
        QVERIFY(appearance->property("draftSoundTrack").toBool());
        QTRY_VERIFY(item("appearance-sound-track") && item("appearance-sound-track")->isVisible());
        // Controls make their theme sounds: a checkbox presses and releases.
        QVERIFY(appearance->setProperty("tab", 4));
        QTRY_VERIFY(item("show-control-strip") && item("show-control-strip")->isVisible());
        auto *box = item("show-control-strip");
        QTest::mouseClick(&view, Qt::LeftButton, {}, box->mapToScene(QPointF(8, box->height() / 2)).toPoint());
        QTRY_COMPARE(played.size(), 4);
        QCOMPARE(played.at(2).first().toString(), QString("chkp"));
        QCOMPARE(played.at(3).first().toString(), QString("chkr"));
        auto *fonts = item("appearance-tab-fonts");
        QVERIFY(fonts);
        QTest::mouseClick(&view, Qt::LeftButton, {}, fonts->mapToScene(QPointF(fonts->width() / 2, fonts->height() / 2)).toPoint());
        QTRY_COMPARE(played.size(), 6);
        QCOMPARE(played.at(4).first().toString(), QString("tabp"));
        QCOMPARE(played.at(5).first().toString(), QString("tabr"));
        QCOMPARE(appearance->property("tab").toInt(), 1);
        QVERIFY(appearance->setProperty("draftSoundTrack", false));
        QVERIFY(QMetaObject::invokeMethod(appearance, "accept"));
        QTRY_VERIFY(!sounds->enabled());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "showMenu", Q_ARG(QVariant, 0)));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "closeMenu", Q_ARG(QVariant, true), Q_ARG(QVariant, QString("mnuc"))));
        QTest::qWait(100);
        QCOMPARE(played.size(), 6);
        QFile saved(root.filePath("desktop-profile/interface-sounds.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly) && saved.readAll().contains("\"None\""));
    }

    void desktopLaunchersOpenOnlyWhenTrusted()
    {
        // Checked when opened, not only when listed: the user's own executable file in Desktop.
        QTemporaryDir root;
        QQuickView view;
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {});
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        QVERIFY(QDir().mkpath(root.path() + "/Desktop") && QDir().mkpath(root.path() + "/Elsewhere"));
        const auto entry = QByteArray("[Desktop Entry]\nType=Application\nName=Probe\nExec=/usr/bin/true\n");
        for (const auto *path : {"/Desktop/inert.desktop", "/Elsewhere/outside.desktop"})
        {
            QFile file(root.path() + path);
            QVERIFY(file.open(QIODevice::WriteOnly) && file.write(entry) == entry.size());
        }
        QVERIFY(QFile::setPermissions(root.path() + "/Elsewhere/outside.desktop",
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        manager.launchDesktopFile(root.path() + "/Desktop/inert.desktop");
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.last().first().toString().contains("not trusted"));
        manager.launchDesktopFile(root.path() + "/Elsewhere/outside.desktop");
        QCOMPARE(errors.size(), 2);
    }
    void batteryModuleShowsTheLaptopsOwnBattery()
    {
        // sysfs power_supply, read-only: the system battery (not a peripheral's) with its estimate.
        QTemporaryDir root;
        const auto supply = [&](const QString &name, const QMap<QString, QString> &files)
        {
            QVERIFY(QDir().mkpath(root.filePath(name)));
            for (auto it = files.begin(); it != files.end(); ++it)
            {
                QFile file(root.filePath(name + "/" + it.key()));
                QVERIFY(file.open(QIODevice::WriteOnly) && file.write((it.value() + "\n").toUtf8()) > 0);
            }
        };
        supply("ACAD", {{"type", "Mains"}, {"online", "0"}});
        supply("hid-mouse-battery", {{"type", "Battery"}, {"scope", "Device"}, {"capacity", "12"}, {"status", "Discharging"}});
        supply("BAT0", {{"type", "Battery"}, {"present", "1"}, {"capacity", "69"}, {"status", "Discharging"},
                        {"energy_now", "30000000"}, {"energy_full", "50000000"}, {"power_now", "12000000"}});
        {
            BatteryStatus battery(root.path());
            QVERIFY(battery.present());
            QCOMPARE(battery.percent(), 69);
            QVERIFY(!battery.charging() && !battery.pluggedIn());
            QCOMPARE(battery.minutesRemaining(), 150); // 30 Wh at 12 W.
        }
        supply("ACAD", {{"online", "1"}});
        supply("BAT0", {{"status", "Charging"}});
        {
            BatteryStatus battery(root.path());
            QVERIFY(battery.charging() && battery.pluggedIn());
            QCOMPARE(battery.minutesRemaining(), 100); // 20 Wh to full at 12 W.
            QSignalSpy changed(&battery, &BatteryStatus::changed);
            supply("BAT0", {{"capacity", "70"}});
            battery.refresh();
            QCOMPARE(changed.size(), 1);
            QCOMPARE(battery.percent(), 70);
        }
        QVERIFY(!BatteryStatus(QString()).present());
        QVERIFY(!BatteryStatus(root.filePath("missing")).present());
        // In the desktop: the Control Strip gains the module, whose menu reads the battery.
        const auto previous = qgetenv("FRANKENSTEIN_POWER_SUPPLY_ROOT");
        const auto restore = qScopeGuard([&] { qputenv("FRANKENSTEIN_POWER_SUPPLY_ROOT", previous); });
        qputenv("FRANKENSTEIN_POWER_SUPPLY_ROOT", root.path().toUtf8());
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.filePath("ws"), root.filePath("desktop-profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        QTRY_VERIFY(item("control-strip-battery"));
        QVariant entries;
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "stripMenuEntries", Q_RETURN_ARG(QVariant, entries),
                                          Q_ARG(QVariant, QString("battery"))));
        const auto list = entries.toList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value("text").toString(), QString("Battery: 70%"));
        QCOMPARE(list.at(1).toMap().value("text").toString(), QString("Charging — 1 hr 40 min until full"));
        // Low battery warns at 10% and again at 5% on battery, once each; plugging in re-arms.
        auto *battery = qobject_cast<BatteryStatus *>(manager.battery());
        QVERIFY(battery);
        auto *message = view.rootObject()->findChild<QObject *>("desktop-message-dialog");
        const auto level = [&](int percent, bool mains)
        {
            supply("ACAD", {{"online", mains ? "1" : "0"}});
            supply("BAT0", {{"status", mains ? "Charging" : "Discharging"}, {"capacity", QString::number(percent)}});
            battery->refresh();
        };
        const auto dismiss = [&]
        {
            QVERIFY(QMetaObject::invokeMethod(message, "close"));
            QTRY_VERIFY(!message->property("visible").toBool());
        };
        level(11, false);
        QTest::qWait(100);
        QVERIFY(!message->property("visible").toBool());
        level(10, false);
        QTRY_VERIFY(message->property("visible").toBool());
        QVERIFY(view.rootObject()->property("statusMessage").toString().startsWith("Your battery is low: 10%"));
        dismiss();
        level(8, false);
        QTest::qWait(100);
        QVERIFY(!message->property("visible").toBool());
        level(5, false);
        QTRY_VERIFY(message->property("visible").toBool());
        dismiss();
        level(4, true);
        level(9, false);
        QTRY_VERIFY(message->property("visible").toBool());
    }

    void alertSoundsUseOriginalBeepPersonalWavsAndFlashWhenSilent()
    {
        // Finishing School Phase 11 and psn_tch's personal assets (docs/alert-sounds.md): the public
        // build has one original sound; personal WAVs in the asset folder join the list; broken
        // files are skipped; volume 0 or no player flashes the menu bar. A fake player records
        // what would play, so no test ever makes a sound.
        QTemporaryDir root;
        const auto profile = root.filePath("profile"), assets = root.filePath("assets");
        QVERIFY(QDir().mkpath(assets + "/sounds"));
        const auto wave = [](const QString &path)
        {
            QFile file(path);
            QByteArray header("RIFF");
            header += QByteArray::fromHex("24000000") + "WAVEfmt " + QByteArray::fromHex("10000000010001002256000044ac00000200100064617461")
                      + QByteArray::fromHex("00000000");
            return file.open(QIODevice::WriteOnly) && file.write(header) == header.size();
        };
        QVERIFY(wave(assets + "/sounds/Sosumi Test.wav"));
        {
            QFile stub(assets + "/sounds/Broken.wav"); // A 14-byte text stub, like two of psn_tch's files.
            QVERIFY(stub.open(QIODevice::WriteOnly) && stub.write("not a sound!!\n") == 14);
            QFile other(assets + "/sounds/notes.txt");
            QVERIFY(other.open(QIODevice::WriteOnly));
        }
        const auto calls = root.filePath("player.calls");
        const auto player = root.filePath("player");
        {
            QFile script(player);
            QVERIFY(script.open(QIODevice::WriteOnly));
            script.write(("#!/bin/sh\nprintf '%s\\n' \"$*\" >> '" + calls + "'\n").toUtf8());
        }
        QVERIFY(QFile::setPermissions(player, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto recorded = [&]
        {
            QFile file(calls);
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed().split('\n', Qt::SkipEmptyParts) : QStringList();
        };
        {
            AlertSounds sounds(profile, assets, player);
            QStringList names;
            for (const auto &sound : sounds.sounds())
                names << sound.toMap().value("name").toString();
            QCOMPARE(names, QStringList({"Simple Beep", "Sosumi Test"}));
            const auto beep = profile + "/sounds/Simple Beep.wav";
            QFile original(beep);
            QVERIFY(original.open(QIODevice::ReadOnly));
            const auto bytes = original.readAll();
            QVERIFY(bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WAVE" && bytes.size() > 4000);
            QCOMPARE(sounds.selected(), QString("Simple Beep"));
            QCOMPARE(sounds.volume(), 60);
            QSignalSpy flashed(&sounds, &AlertSounds::flash);
            sounds.play();
            QTRY_COMPARE(recorded(), QStringList({"--volume 0.60 " + beep}));
            sounds.choose("Sosumi Test");
            QTRY_COMPARE(recorded().size(), 2);
            QCOMPARE(recorded().last(), "--volume 0.60 " + assets + "/sounds/Sosumi Test.wav");
            sounds.choose("Broken");
            sounds.choose("Missing");
            QCOMPARE(sounds.selected(), QString("Sosumi Test"));
            sounds.setVolume(0);
            sounds.play();
            QCOMPARE(flashed.size(), 1);
            QTest::qWait(200);
            QCOMPARE(recorded().size(), 2);
            sounds.setVolume(35);
        }
        {
            // The choice and volume persist; the original sound is rewritten identically.
            AlertSounds reopened(profile, assets, player);
            QCOMPARE(reopened.selected(), QString("Sosumi Test"));
            QCOMPARE(reopened.volume(), 35);
            AlertSounds silent(profile, assets, QString());
            QSignalSpy flashed(&silent, &AlertSounds::flash);
            silent.play();
            QCOMPARE(flashed.size(), 1);
        }
        // In the desktop: alerts sound when they open, a click outside a modal dialog sounds the
        // alert, and without a player the menu bar flashes.
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("desktop-profile"), {});
        auto *alerts = manager.alertSounds();
        QVERIFY(alerts);
        QSignalSpy flashed(alerts, SIGNAL(flash()));
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        auto *message = view.rootObject()->findChild<QObject *>("desktop-message-dialog");
        QVERIFY(QMetaObject::invokeMethod(message, "open"));
        QTRY_COMPARE(flashed.size(), 1);
        QTRY_VERIFY(item("menu-bar-flash") && item("menu-bar-flash")->isVisible());
        QTRY_VERIFY(!item("menu-bar-flash")->isVisible());
        // A click inside the dialog is not an alert; one outside it is.
        auto *body = qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(message->property("contentItem")));
        QVERIFY(body);
        QTest::mouseClick(&view, Qt::LeftButton, {}, body->mapToScene(QPointF(4, 4)).toPoint());
        QTest::qWait(200);
        QCOMPARE(flashed.size(), 1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(40, 600));
        QTRY_COMPARE(flashed.size(), 2);
        QVERIFY(message->property("visible").toBool()); // The outside click changed nothing else.
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!message->property("visible").toBool());
        QTRY_VERIFY(!item("menu-bar-flash")->isVisible());
        // The Sound panel's Alert Sounds tab (first, as in Mac OS 9) lists and plays the sounds.
        AlertSounds audible(profile, assets, player);
        AudioService absent(root.filePath("absent-audio.py"));
        auto *panel = view.rootObject()->findChild<QObject *>("sound-panel");
        QVERIFY(panel);
        QVERIFY(panel->setProperty("alertSounds", QVariant::fromValue(static_cast<QObject *>(&audible))));
        QVERIFY(panel->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&absent))));
        QVERIFY(QMetaObject::invokeMethod(panel, "edit"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(!panel->property("alerts").toBool()); // It opens on Output.
        auto *tab = item("sound-tab-alerts");
        QVERIFY(tab);
        QTest::mouseClick(&view, Qt::LeftButton, {}, tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint());
        QTRY_VERIFY(panel->property("alerts").toBool());
        QQuickItem *beep = nullptr;
        QTRY_VERIFY((beep = item("sound-alert-Simple Beep")) && beep->isVisible());
        QVERIFY(item("sound-alert-folder")->property("text").toString().contains(assets + "/sounds"));
        const auto before = recorded().size();
        QTest::mouseClick(&view, Qt::LeftButton, {}, beep->mapToScene(QPointF(40, beep->height() / 2)).toPoint());
        QTRY_COMPARE(audible.selected(), QString("Simple Beep"));
        QTRY_COMPARE(recorded().size(), before + 1);
        QVERIFY(recorded().last().endsWith("/sounds/Simple Beep.wav"));
        // Clicks inside the panel never flash the menu bar.
        QCOMPARE(flashed.size(), 2);
        QVERIFY(!item("menu-bar-flash")->isVisible());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/sound-alerts.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void monitorsPanelArrangesDisplaysAndAsksTheSupervisorForTheMainOne()
    {
        // Mac OS 9 Monitors → Arrange: drag the menu bar to choose the main display. The request
        // goes to the private session supervisor's control socket (here a fake), and the desktop
        // follows only the topology the supervisor then publishes.
        QTemporaryDir root;
        const auto control = root.filePath("control");
        QLocalServer supervisor;
        QVERIFY(supervisor.listen(control));
        QByteArrayList commands;
        bool accept = true;
        connect(&supervisor, &QLocalServer::newConnection, &supervisor, [&] {
            auto *client = supervisor.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, client, [&, client] {
                if (!client->canReadLine())
                    return;
                commands.append(client->readLine());
                client->write(QJsonDocument(QJsonObject{{"ok", accept}}).toJson(QJsonDocument::Compact));
                client->flush();
                client->disconnectFromServer();
            });
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        });
        qputenv("PLATINUM_SESSION_CONTROL", control.toLocal8Bit());
        const auto unset = qScopeGuard([] { qunsetenv("PLATINUM_SESSION_CONTROL"); });
        QQuickView view;
        // As in the Extend session, one host window spans both displays (1120 + 800 wide), and
        // as in production the root object follows the window.
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1920, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(!manager.displayArrangementAvailable()); // No published topology yet.
        const auto layout = [](const QString &primary) {
            return *DisplayLayout::create({{"DP-3", QRect(0, 0, 1120, 760), 1.5, "normal"},
                                           {"eDP-1", QRect(1120, 0, 800, 500), 2, "normal"}},
                                          primary, DisplayLayout::Mode::Extend);
        };
        manager.setDisplayLayout(layout("DP-3"));
        QVERIFY(manager.displayArrangementAvailable());
        const auto outputs = manager.displayOutputs();
        QCOMPARE(outputs.size(), 2);
        QVERIFY(outputs.at(1).toMap().value("internal").toBool());
        QVERIFY(outputs.at(0).toMap().value("primary").toBool());
        auto *panel = view.rootObject()->findChild<QObject *>("monitors-panel");
        QVERIFY(panel);
        const auto item = [&](const QString &name) {
            view.grabWindow();
            return visualItem(view.contentItem(), name);
        };
        const auto centre = [&](QQuickItem *target, QPointF at = {}) {
            return target->mapToScene(at.isNull() ? QPointF(target->width() / 2, target->height() / 2) : at).toPoint();
        };
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        QQuickItem *external = nullptr, *builtIn = nullptr, *menuBar = nullptr;
        QTRY_VERIFY((external = item("monitors-display-DP-3")) && (builtIn = item("monitors-display-eDP-1")) &&
                    (menuBar = item("monitors-menubar")) && menuBar->width() > 0);
        // Drawn to scale: the external display is the wider one, the menu bar sits on it.
        QVERIFY(external->width() > builtIn->width());
        QCOMPARE(qRound(menuBar->y()), qRound(external->y()) + 1);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/monitors-panel.png"));
        // Drag the menu bar onto the built-in display.
        const auto from = centre(menuBar);
        const auto to = centre(builtIn, QPointF(builtIn->width() / 2, 6));
        QTest::mousePress(&view, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 8; ++step)
            QTest::mouseMove(&view, from + (to - from) * step / 8, 10);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, to, 10);
        QTRY_COMPARE(panel->property("draftPrimary").toString(), QString("eDP-1"));
        QTRY_COMPARE(qRound(menuBar->x()), qRound(builtIn->x()) + 1);
        QVERIFY(manager.primaryDisplayRect().x() == 0); // Nothing changes until OK and publication.
        auto *ok = item("monitors-panel-accept");
        QVERIFY(ok);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(ok));
        QTRY_COMPARE(commands.size(), 1);
        QCOMPARE(commands.first(), QByteArray("primary eDP-1\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        // The supervisor publishes the new topology; the menu bar follows it.
        manager.setDisplayLayout(layout("eDP-1"));
        QCOMPARE(manager.primaryDisplayRect(), QRect(1120, 0, 800, 500));
        QTRY_COMPARE(qRound(item("desktop-menu-bar")->x()), 1120);
        // A refused request is reported; the button path works without dragging.
        accept = false;
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_VERIFY((external = item("monitors-display-DP-3")) && external->width() > 0);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(external));
        QTRY_COMPARE(panel->property("selected").toString(), QString("DP-3"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(item("monitors-make-main")));
        QCOMPARE(panel->property("draftPrimary").toString(), QString("DP-3"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(item("monitors-panel-accept")));
        QTRY_COMPARE(commands.size(), 2);
        QCOMPARE(commands.last(), QByteArray("primary DP-3\n"));
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors.first().first().toString(), QString("The main display could not be changed."));
        QCOMPARE(manager.primaryDisplayRect().x(), 1120);
        // The refusal is shown in the desktop's modal alert; dismiss it first.
        QTRY_VERIFY(manager.shellModal());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        // Mirroring is shown, honestly unavailable.
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        auto *mirror = item("monitors-mode-mirror");
        QVERIFY(mirror && !mirror->isEnabled());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(commands.size(), 2);
    }
    void appearancePanelIsTabbedWithVariationsAndOptions()
    {
        // docs/historical-fidelity-review.md F10 (V922 E17/E18): Appearance · Fonts · Desktop ·
        // Sound · Options, the graphite highlight variation (V922 E11) and the collapse option.
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *panel = view.rootObject()->findChild<QObject *>("appearance-dialog");
        QVERIFY(panel);
        const auto choose = [&](const QString &name)
        {
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                view.grabWindow();
                auto *item = visualItem(view.contentItem(), name);
                if (item && item->isVisible() && item->isEnabled() && item->width() > 0)
                {
                    QTest::mouseClick(&view, Qt::LeftButton, {},
                                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    return true;
                }
                QTest::qWait(20);
            }
            return false;
        };
        const auto shown = [&](const QString &name)
        {
            view.grabWindow();
            auto *item = visualItem(view.contentItem(), name);
            return item && item->isVisible();
        };
        const auto open = [&](const QString &page)
        {
            QMetaObject::invokeMethod(panel, "edit", Q_ARG(QVariant, QVariant(page)));
            return QTest::qWaitFor([&] { return panel->property("opened").toBool(); }, 3000);
        };
        QCOMPARE(settings->accent(), QString("blue"));
        QVERIFY(settings->doubleClickCollapse());
        QVERIFY(!settings->keepScreenAwake());
        QCOMPARE(view.rootObject()->property("menuAccent").value<QColor>(), QColor("#2c2994"));

        QVERIFY(open("Appearance"));
        for (const auto *tab : {"appearance", "fonts", "desktop", "sound", "options"})
            QVERIFY2(shown(QString("appearance-tab-") + tab), tab);
        QVERIFY(!shown("appearance-tab-themes")); // One theme only: no Themes tab.
        QVERIFY(shown("appearance-accent-sample"));
        QVERIFY(choose("appearance-accent-graphite"));
        QCOMPARE(panel->property("draftAccent").toString(), QString("graphite"));
        QTest::qWait(300); // Evidence only: the tab canvases and wallpaper paint on later frames.
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/appearance-tabs.png"));
        QVERIFY(choose("appearance-tab-fonts"));
        QTRY_VERIFY(shown("appearance-font-large"));
        QVERIFY(!shown("appearance-accent-sample"));
        QVERIFY(choose("appearance-tab-sound"));
        QVERIFY(choose("appearance-tab-options"));
        QTRY_VERIFY(shown("option-double-click-collapse"));
        QVERIFY(choose("keep-screen-awake"));
        QVERIFY(panel->property("draftKeepAwake").toBool());
        QVERIFY(choose("option-double-click-collapse"));
        QVERIFY(!panel->property("draftDoubleClick").toBool());
        QVERIFY(choose("appearance-dialog-accept"));
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(settings->accent(), QString("graphite"));
        QVERIFY(!settings->doubleClickCollapse());
        QCOMPARE(view.rootObject()->property("menuAccent").value<QColor>(), QColor("#656565"));
        AppearanceSettings reloaded(root.path() + "/profile");
        QCOMPARE(reloaded.accent(), QString("graphite"));
        QVERIFY(!reloaded.doubleClickCollapse());
        QVERIFY(settings->keepScreenAwake());
        QVERIFY(reloaded.keepScreenAwake());

        // Cancel keeps the saved choices.
        QVERIFY(open("Appearance"));
        QVERIFY(choose("appearance-accent-blue"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(settings->accent(), QString("graphite"));

        QVERIFY(open("Options"));
        QVERIFY(choose("keep-screen-awake"));
        QVERIFY(!panel->property("draftKeepAwake").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(settings->keepScreenAwake()); // Cancel must not change the saved choice.
        QVERIFY(open("Options"));
        QVERIFY(choose("keep-screen-awake"));
        QVERIFY(choose("appearance-dialog-accept"));
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(!settings->keepScreenAwake());
        AppearanceSettings awakeOff(root.path() + "/profile");
        QVERIFY(!awakeOff.keepScreenAwake());

        // Desktop: Remove Picture applies at once; Place Picture… hands over to the chooser.
        // (A new profile has no picture; give it one to remove.)
        QImage picture(32, 24, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        QVERIFY(picture.save(root.path() + "/picture.png"));
        QVERIFY(settings->applyDesktopPicture(root.path() + "/picture.png", "fill"));
        QVERIFY(open("Desktop"));
        QCOMPARE(panel->property("tab").toInt(), 2);
        QVERIFY(!settings->wallpaperPath().isEmpty());
        QVERIFY(choose("appearance-remove-picture"));
        QTRY_VERIFY(settings->wallpaperPath().isEmpty());
        QVERIFY(choose("appearance-place-picture"));
        auto *chooser = view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(chooser);
        QTRY_VERIFY(chooser->property("opened").toBool());
        QVERIFY(!panel->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
    }
    void controlStripAttachesToEdgesFoldsAndOpensPanelsFromModuleMenus()
    {
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(
            view.status() != QQuickView::Error,
            qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *strip = visualItem(view.contentItem(), "control-strip");
        QVERIFY(strip && strip->isVisible());
        auto click = [&](const QString &name)
        {
            auto *item = visualItem(view.contentItem(), name);
            if (!item)
                return false;
            QTest::mouseClick(
                &view, Qt::LeftButton, {},
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        // Rows of the module menus appear after a polish; retry until one is clickable.
        const auto choose = [&](const QString &name)
        {
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                view.grabWindow();
                auto *item = visualItem(view.contentItem(), name);
                if (item && item->isVisible() && item->width() > 0)
                    return click(name);
                QTest::qWait(20);
            }
            return false;
        };
        const auto sceneX = [&](const QString &name)
        { return visualItem(view.contentItem(), name)->mapToScene(QPointF(0, 0)).x(); };
        // Mac OS 9 attaches the strip to a side edge: by default the bottom-left corner, with the
        // collapse box at the screen edge and the end tab at the far end (V922 E04/E12).
        QCOMPARE(strip->position(), QPointF(0, view.height() - strip->height()));
        QVERIFY(sceneX("control-strip-collapse") < sceneX("control-strip-sound"));
        QVERIFY(sceneX("control-strip-sound") < sceneX("control-strip-tab"));
        const auto expandedWidth = strip->width();
        QVERIFY(click("control-strip-collapse"));
        QTRY_VERIFY(settings->controlStripCollapsed());
        QCOMPARE(strip->width(), 16);
        QCOMPARE(strip->x(), 0);
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(!settings->controlStripCollapsed());
        QCOMPARE(strip->width(), expandedWidth);
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(settings->controlStripCollapsed());
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(!settings->controlStripCollapsed());
        // Every module fits, so both scroll arrows are inactive.
        QVERIFY(!visualItem(view.contentItem(), "control-strip-scroll-back")->property("active").toBool());
        QVERIFY(!visualItem(view.contentItem(), "control-strip-scroll-forward")->property("active").toBool());
        const auto drag = [&](QPoint offset)
        {
            auto *tab = visualItem(view.contentItem(), "control-strip-tab");
            const auto point = tab->mapToScene(QPointF(8, 14)).toPoint();
            QTest::mousePress(&view, Qt::LeftButton, {}, point);
            QTest::mouseMove(&view, point + offset / 2, 30);
            QTest::mouseMove(&view, point + offset, 30);
            QTest::mouseRelease(&view, Qt::LeftButton, {}, point + offset, 30);
        };
        // A short drag moves the strip vertically and snaps it back to the nearer edge.
        const auto old = strip->position();
        drag(QPoint(90, -100));
        QTRY_COMPARE(strip->position(), QPointF(0, old.y() - 100));
        AppearanceSettings position(root.path() + "/profile");
        QCOMPARE(position.controlStripX(), 0);
        QCOMPARE(position.controlStripY(), qRound(strip->y()));
        // Past the middle it attaches to the right edge, mirrored, and stays there when folded.
        drag(QPoint(800, 0));
        QTRY_COMPARE(strip->x(), view.width() - strip->width());
        QCOMPARE(strip->y(), old.y() - 100);
        QVERIFY(sceneX("control-strip-tab") < sceneX("control-strip-sound"));
        QVERIFY(sceneX("control-strip-sound") < sceneX("control-strip-collapse"));
        AppearanceSettings right(root.path() + "/profile");
        QCOMPARE(right.controlStripX(), qRound(strip->x()));
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(settings->controlStripCollapsed());
        QCOMPARE(strip->x(), view.width() - 16);
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(!settings->controlStripCollapsed());
        QCOMPARE(strip->x(), view.width() - expandedWidth);
        drag(QPoint(-800, 0));
        QTRY_COMPARE(strip->x(), 0);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/control-strip.png"));
        // Modules open pop-up menus above the strip; the first item opens the owning panel.
        QVERIFY(click("control-strip-appearance"));
        auto *menu = visualItem(view.contentItem(), "control-strip-menu");
        QVERIFY(menu);
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(qRound(menu->y() + menu->height()), qRound(strip->y()));
        QCOMPARE(strip->property("openModule").toString(), QString("appearance"));
        QVERIFY(choose("desktop-command-appearance"));
        auto *panel = view.rootObject()->findChild<QObject *>("appearance-dialog");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(manager.shellModal());
        // The panel opens on its Appearance tab; WindowShade speed lives under Options.
        QCOMPARE(panel->property("tab").toInt(), 0);
        QVERIFY(choose("appearance-tab-options"));
        QTRY_COMPARE(panel->property("tab").toInt(), 4);
        panel->setProperty("draftDuration", 600);
        QVERIFY(choose("shade-preview-button"));
        auto *preview = view.rootObject()->findChild<QQuickItem *>("shade-preview");
        QVERIFY(preview);
        QTRY_VERIFY(preview->property("animating").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/appearance.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(settings->shadeDuration(), 240);
        QVERIFY(click("control-strip-applications"));
        QVERIFY(choose("desktop-command-applications"));
        auto *browser = view.rootObject()->findChild<QObject *>("application-browser");
        QVERIFY(browser);
        QTRY_VERIFY(browser->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(click("control-strip-appearance"));
        QVERIFY(choose("desktop-command-appearance"));
        QTRY_VERIFY(panel->property("opened").toBool());
        panel->setProperty("draftDuration", 600);
        panel->setProperty("draftStripVisible", false);
        QVERIFY(click("appearance-dialog-accept"));
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(settings->shadeDuration(), 600);
        QVERIFY(!strip->isVisible());
        // The menu provides a way back when the strip is hidden: Apple menu >
        // Control Panels, which also shows the strip's state with a check mark.
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(25, 13));
        auto *panels = visualItem(view.contentItem(), "desktop-command-control-panels");
        QVERIFY(panels);
        panels->parentItem()->ensurePolished();
        QVERIFY(click("desktop-command-control-panels"));
        auto *stripCommand = visualItem(view.contentItem(), "desktop-command-control-strip");
        QTRY_VERIFY(stripCommand && stripCommand->isVisible());
        stripCommand->parentItem()->ensurePolished();
        auto *entry = visualItem(view.contentItem(), "desktop-command-appearance");
        QVERIFY(entry);
        entry->parentItem()->ensurePolished();
        QVERIFY(click("desktop-command-appearance"));
        QTRY_VERIFY(panel->property("opened").toBool());
        panel->setProperty("draftStripVisible", true);
        QVERIFY(click("appearance-dialog-accept"));
        QTRY_VERIFY(strip->isVisible());
        auto *bar = visualItem(view.contentItem(), "bar-entry-1");
        QVERIFY(bar);
        bool foundFont = false;
        for (auto *child : bar->childItems())
            if (child->property("text") == "File")
            {
                QCOMPARE(child->property("font").value<QFont>().family(),
                         classicInterfaceFontFamily());
                foundFont = true;
            }
        QVERIFY(foundFont);
    }
    void applicationBrowserSearchCancelAndPreviewLaunchGuard()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        const auto directory = fixture.path() + "/applications";
        QVERIFY(QDir().mkpath(directory));
        QFile entry(directory + "/test.desktop");
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Example Program\nComment=A native "
                    "application\nExec=/usr/bin/true %f\nMimeType=text/plain;\n");
        entry.close();
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {},
                              {directory});
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(catalog);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(25, 13));
        auto *command = visualItem(view.contentItem(), "desktop-command-applications");
        QVERIFY(command);
        command->parentItem()->ensurePolished();
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          command->mapToScene(QPointF(70, 12)).toPoint());
        auto *browser = view.rootObject()->findChild<QObject *>("application-browser");
        QVERIFY(browser);
        QTRY_VERIFY(browser->property("opened").toBool());
        QTRY_VERIFY(catalog->ready());
        QVERIFY(manager.shellModal());
        auto *search = view.rootObject()->findChild<QQuickItem *>("application-search");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(typeAscii(&view, "absent"));
        QTRY_COMPARE(catalog->count(), 0);
        QVERIFY(!browser->property("acceptEnabled").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QVERIFY(browser->property("opened").toBool());
        commandKey(&view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&view, "Example"));
        QTRY_COMPARE(catalog->count(), 1);
        QTRY_VERIFY(browser->property("acceptEnabled").toBool());
        auto selected = browser->property("selected");
        if (selected.metaType() == QMetaType::fromType<QJSValue>())
            selected = selected.value<QJSValue>().toVariant();
        QCOMPARE(selected.toMap().value("name").toString(), QString("Example Program"));
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QTest::qWait(30); // Let Canvas delegates paint before the diagnostic capture.
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/applications.png"));
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!browser->property("visible").toBool());
        QTRY_COMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains("native nested session"));
        QCOMPARE(manager.rowCount(), 0);
        auto *message = view.rootObject()->findChild<QObject *>("desktop-message-dialog");
        QVERIFY(message);
        QTRY_VERIFY(message->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
    }
    void applicationsOpenTheExistingBluetoothPanelWithoutLaunchingAProgram()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.filePath("profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clear = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *browser = view.rootObject()->findChild<QObject *>("application-browser");
        auto *panel = view.rootObject()->findChild<QObject *>("bluetooth-panel");
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(browser && panel && catalog);
        const auto service = panel->property("service").value<QObject *>();
        QCOMPARE(service, manager.bluetooth());
        QSignalSpy requested(&manager, &NativeManager::controlPanelRequested);
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        altKey(&view, Qt::Key_D, 32);
        QTRY_VERIFY(browser->property("opened").toBool());
        QTRY_VERIFY(catalog->ready());
        auto *search = visualItem(view.contentItem(), "application-search");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(typeAscii(&view, "bluetooth"));
        QTRY_COMPARE(catalog->count(), 1);
        QVERIFY(browser->property("acceptEnabled").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(!browser->property("visible").toBool());
        QVERIFY(manager.shellModal());
        QCOMPARE(requested.count(), 1);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(manager.rowCount(), 0); // Internal panel, no application process.
        QCOMPARE(panel->property("service").value<QObject *>(), service);
        // An active modal dialog still guards direct/recent-application requests.
        manager.launchApplication("org.frankenstein.Bluetooth.desktop");
        QCOMPARE(requested.count(), 1);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        const auto document = fixture.filePath("file.txt");
        QFile file(document);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("text"); file.close();
        manager.launchApplication("org.frankenstein.Bluetooth.desktop", document);
        QCOMPARE(requested.count(), 1);
        QCOMPARE(errors.count(), 1);
        QVERIFY(!panel->property("visible").toBool());
    }
    void openWithRequestsQueueWithoutLosingTheSecondDialog()
    {
        QTemporaryDir fixture, outside;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto first = fixture.path() + "/First.txt", second = fixture.path() + "/Second.txt";
        for (const auto &path : {first, second, outside.path() + "/Outside.txt"})
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("document");
        }
        QVERIFY(!manager.readableDocument(outside.path() + "/Outside.txt"));
        manager.requestOpenDocument(first, true);
        manager.requestOpenDocument(second, true);
        auto *browser = view.rootObject()->findChild<QObject *>("application-browser");
        QVERIFY(browser);
        QTRY_VERIFY(browser->property("opened").toBool());
        QCOMPARE(browser->property("documentPath").toString(), first);
        QVERIFY(browser->property("acceptEnabled").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/open-with.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_COMPARE(browser->property("documentPath").toString(), second);
        QTRY_VERIFY(browser->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(!browser->property("visible").toBool());
    }
    void fixedAndModalWindowsKeepUsefulClientSizeConstraints()
    {
        const auto alert = windowPresentation(true, {280, 110}, {280, 110});
        QVERIFY(!alert.resize && !alert.zoom && !alert.closeBox);
        const auto picker = windowPresentation(true, {400, 300}, {0, 0});
        QVERIFY(picker.resize && !picker.zoom && !picker.closeBox);
        const auto fixedTool = windowPresentation(false, {80, 60}, {80, 60});
        QVERIFY(!fixedTool.resize && !fixedTool.zoom && fixedTool.closeBox);
        const auto narrow = windowPresentation(false, {80, 60}, {80, 500});
        QVERIFY(narrow.resize && narrow.zoom && narrow.closeBox);
        QCOMPARE(boundedContentSize({900, 900}, {80, 60}, {80, 500}), QSize(80, 500));
        QCOMPARE(boundedContentSize({20, 20}, {80, 60}, {80, 500}), QSize(80, 60));
        QCOMPARE(boundedContentSize({900, 900}, {}, {100, 70}), QSize(100, 70));
    }
    void disabledMenusAndBlankBarDoNotClearDesktopSelection()
    {
        QTemporaryDir fixture;
        QQuickView view;
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        items->select("volume");
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const QVariantList entries{
            QVariantMap{{"label", "◆"}, {"kind", "apple"}},
            QVariantMap{{"label", "File"}, {"kind", "export"}, {"enabled", false}}};
        QVERIFY(view.rootObject()->setProperty("barEntries", entries));
        auto *disabled = visualItem(view.contentItem(), "bar-entry-1");
        QVERIFY(disabled);
        disabled->parentItem()->ensurePolished();
        QVERIFY(disabled->x() > 0);
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          disabled->mapToScene(QPointF(20, 13)).toPoint());
        QCOMPARE(items->selectionCount(), 1);
        QVERIFY(!manager.menuOpen());
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(650, 13));
        QCOMPARE(items->selectionCount(), 1);
        QVERIFY(!manager.menuOpen());
    }
    void desktopRightClickPreservesGroupsAndRunsBackgroundCommands()
    {
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QFile file(items->directory() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        items->refresh();
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *one = visualItem(view.contentItem(), "desktop-entry-One.txt");
        auto *two = visualItem(view.contentItem(), "desktop-entry-Two.txt");
        QVERIFY(one && two);
        QTest::mouseClick(&view, Qt::LeftButton, {}, one->mapToScene(QPointF(50, 20)).toPoint());
        QTest::mouseClick(&view, Qt::LeftButton, Qt::ShiftModifier,
                          two->mapToScene(QPointF(50, 20)).toPoint());
        QCOMPARE(items->selectionCount(), 2);
        QTest::mouseClick(&view, Qt::RightButton, {}, one->mapToScene(QPointF(50, 20)).toPoint());
        QTRY_VERIFY(manager.menuOpen());
        QCOMPARE(items->selectionCount(), 2);
        QVERIFY(!items->dragging());
        auto *rename = visualItem(view.contentItem(), "context-rename");
        auto *duplicate = visualItem(view.contentItem(), "context-duplicate");
        QVERIFY(rename && duplicate);
        QVERIFY(!rename->isEnabled());
        QVERIFY(duplicate->isEnabled());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/desktop-context.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.menuOpen());
        QTest::mouseClick(&view, Qt::RightButton, {}, QPoint(240, 400));
        QTRY_VERIFY(manager.menuOpen());
        QCOMPARE(items->selectionCount(), 0);
        auto *create = visualItem(view.contentItem(), "context-new-folder");
        QVERIFY(create);
        create->parentItem()->ensurePolished();
        QTRY_VERIFY(create->width() > 0 && create->isVisible());
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          create->mapToScene(QPointF(create->width() / 2, 12)).toPoint());
        QTRY_VERIFY(manager.shellModal());
        QVERIFY(!manager.menuOpen());
        auto *field = view.rootObject()->findChild<QQuickItem *>("desktop-name-field");
        QVERIFY(field);
        QTRY_VERIFY(field->hasActiveFocus());
        commandKey(&view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&view, "Context folder"));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!manager.shellModal() && !items->busy());
        QTRY_VERIFY(QFileInfo(items->directory() + "/Context folder").isDir());
        QVERIFY(!QFileInfo(items->directory() + "/One.txt").isDir());
    }
    void desktopMarqueeKeepsGroupsThroughShiftCancelAndRealFolderDrops()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(QDir(items->directory()).mkdir("Target"));
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QFile file(items->directory() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        items->refresh();
        // Fixture placement only; all selection and transfer actions use input events.
        int x = 180;
        for (const auto &name : {"One.txt", "Two.txt", "Target"})
            for (int row = 0; row < items->rowCount(); ++row)
            {
                const auto index = items->index(row);
                if (items->data(index, DesktopItems::NameRole) != name)
                    continue;
                items->clearSelection();
                items->select(items->data(index, DesktopItems::KeyRole).toString());
                items->moveSelected(x - items->data(index, DesktopItems::XRole).toInt(),
                                    220 - items->data(index, DesktopItems::YRole).toInt());
                x += 180;
            }
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTest::mousePress(&view, Qt::LeftButton, {}, {160, 200});
        QTest::mouseMove(&view, {470, 310}, 40);
        QVERIFY(items->selecting());
        QCOMPARE(items->selectionCount(), 2);
        auto *rectangle = visualItem(view.contentItem(), "desktop-selection-rectangle");
        QVERIFY(rectangle && rectangle->isVisible());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/desktop-marquee.png"));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, {470, 310});
        QVERIFY(!items->selecting());
        QCOMPARE(items->selectionCount(), 2);
        QTest::mousePress(&view, Qt::LeftButton, Qt::ShiftModifier, {520, 200});
        QTest::mouseMove(&view, {660, 310}, 40);
        QCOMPARE(items->selectionCount(), 3);
        QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(items->selectionCount(), 2);
        QVERIFY(!items->selecting());
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::ShiftModifier, {660, 310});
        auto *source = visualItem(view.contentItem(), "desktop-entry-One.txt");
        auto *target = visualItem(view.contentItem(), "desktop-entry-Target");
        QVERIFY(source && target);
        const auto start = source->mapToScene(QPointF(50, 20)).toPoint();
        const auto end = target->mapToScene(QPointF(50, 20)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, start);
        QCOMPARE(items->selectionCount(), 2);
        QTest::mouseMove(&view, end, 40);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, end);
        QTRY_VERIFY(!items->busy());
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QVERIFY(!QFileInfo::exists(items->directory() + '/' + name));
            QFile moved(items->directory() + "/Target/" + name);
            QVERIFY(moved.open(QIODevice::ReadOnly));
            QCOMPARE(moved.readAll(), QByteArray(name));
        }
        QTest::mouseClick(&view, Qt::LeftButton, {}, {160, 400});
        QCOMPARE(items->selectionCount(), 0);
    }
    void actualIconDropMovesTheFileIntoTheTargetFolder()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(QDir(items->directory()).mkdir("Target"));
        const auto path = items->directory() + "/Move me.txt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("native file drop");
        file.close();
        items->refresh();
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *source = visualItem(view.contentItem(), "desktop-entry-Move me.txt");
        auto *target = visualItem(view.contentItem(), "desktop-entry-Target");
        QVERIFY(source && target);
        const auto start = source->mapToScene(QPointF(50, 20)).toPoint();
        const auto finish = target->mapToScene(QPointF(50, 20)).toPoint();
        const auto original = source->position();
        QTest::mousePress(&view, Qt::LeftButton, {}, start);
        QTest::mouseMove(&view, start + QPoint(-150, 40), 40);
        QVERIFY(items->dragging());
        QTest::keyClick(&view, Qt::Key_Escape);
        QVERIFY(!items->dragging());
        QCOMPARE(source->position(), original);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, start + QPoint(-150, 40));
        QVERIFY(QFileInfo(path).exists());
        QTest::mousePress(&view, Qt::LeftButton, {}, start);
        QTest::mouseMove(&view, finish, 40);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, finish);
        QTRY_VERIFY(!items->busy() &&
                    QFileInfo(items->directory() + "/Target/Move me.txt").isFile());
        QVERIFY(!QFileInfo(path).exists());
    }
    void shellDialogsCreateCancelAndRenameActualDesktopItems()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto clickFileCommand = [&](const QString &action)
        {
            auto *fileMenu = visualItem(view.contentItem(), "bar-entry-1");
            if (!fileMenu)
                return false;
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              fileMenu->mapToScene(QPointF(20, 13)).toPoint());
            auto *command = visualItem(view.contentItem(), "desktop-command-" + action);
            if (!command || !command->isVisible() || !command->isEnabled())
                return false;
            command->parentItem()->ensurePolished();
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              command->mapToScene(QPointF(50, 12)).toPoint());
            return true;
        };
        QVERIFY(clickFileCommand("new-folder"));
        auto *dialog = view.rootObject()->findChild<QObject *>("desktop-name-dialog");
        auto *field = view.rootObject()->findChild<QQuickItem *>("desktop-name-field");
        QVERIFY(dialog && field);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(manager.shellModal());
        QTRY_VERIFY(field->hasActiveFocus());
        commandKey(&view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&view, "Cancelled creation"));
        QCOMPARE(field->property("text").toString(), QString("Cancelled creation"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(!QFileInfo(items->directory() + "/Cancelled creation").exists());
        QVERIFY(clickFileCommand("new-folder"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(field->hasActiveFocus());
        commandKey(&view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&view, "Created on Desktop"));
        QCOMPARE(field->property("text").toString(), QString("Created on Desktop"));
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/desktop-naming.png"));
        auto *accept = visualItem(view.contentItem(), "desktop-name-dialog-accept");
        QVERIFY(accept);
        QSignalSpy completed(items, &DesktopItems::operationFinished);
        QTest::mouseClick(
            &view, Qt::LeftButton, {},
            accept->mapToScene(QPointF(accept->width() / 2, accept->height() / 2)).toPoint());
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(!items->busy());
        QCOMPARE(completed.count(), 1);
        QVERIFY(QFileInfo(items->directory() + "/Created on Desktop").isDir());
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(clickFileCommand("rename"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
        QVERIFY(typeAscii(&view, "Cancelled rename"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(QFileInfo(items->directory() + "/Created on Desktop").isDir());
        QVERIFY(!QFileInfo(items->directory() + "/Cancelled rename").exists());
        QVERIFY(clickFileCommand("rename"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
        QVERIFY(typeAscii(&view, "Renamed on Desktop"));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!items->busy() &&
                    QFileInfo(items->directory() + "/Renamed on Desktop").isDir());
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(!QFileInfo(items->directory() + "/Created on Desktop").exists());
        QVERIFY(clickFileCommand("new-folder"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(field->hasActiveFocus());
        commandKey(&view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&view, "Return created"));
        QCOMPARE(field->property("text").toString(), QString("Return created"));
        QSignalSpy accepted(dialog, SIGNAL(accepted()));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(accepted.count(), 1);
        QTRY_VERIFY(!items->busy() && QFileInfo(items->directory() + "/Return created").isDir());
    }
    void nestedShellErrorsRetainInputUntilBothDialogsClose()
    {
        QTemporaryDir fixture;
        QQuickView view;
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(
            QMetaObject::invokeMethod(view.rootObject(), "promptName", Q_ARG(QVariant, "create")));
        auto *name = view.rootObject()->findChild<QObject *>("desktop-name-dialog");
        auto *message = view.rootObject()->findChild<QObject *>("desktop-message-dialog");
        QVERIFY(name && message);
        QTRY_VERIFY(name->property("opened").toBool());
        QVERIFY(!items->renameItem("missing-item", "new name"));
        QTRY_VERIFY(message->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!message->property("visible").toBool());
        QVERIFY(name->property("opened").toBool());
        QVERIFY(manager.shellModal());
        QVERIFY(!manager.activateDesktop());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
    }
    void sessionExitWaitsForDesktopJobsAndModalTokens()
    {
        QTemporaryDir fixture;
        QQuickView view;
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        const auto first = manager.beginShellModal(), second = manager.beginShellModal();
        QVERIFY(first != second);
        manager.endShellModal(first);
        manager.endShellModal(first); // Releasing an old token cannot close the second dialog.
        QVERIFY(manager.shellModal());
        manager.quitSession();
        QCOMPARE(errors.count(), 1);
        manager.endShellModal(second);
        QVERIFY(!manager.shellModal());
        QVERIFY(items->createFolder("Still running"));
        QVERIFY(items->busy());
        manager.quitSession();
        QCOMPARE(errors.count(), 2);
        QTRY_VERIFY(!items->busy());
        QVERIFY(QFileInfo(items->directory() + "/Still running").isDir());
    }
    void realDesktopIconCanBeSelectedMovedAndRestored()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        QFile document(items->directory() + "/Desktop note.txt");
        QVERIFY(document.open(QIODevice::WriteOnly));
        document.write("An ordinary Linux document.");
        document.close();
        items->refresh();
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(visualItem(view.contentItem(), "desktop-entry-Desktop note.txt"));
        auto *entry = visualItem(view.contentItem(), "desktop-entry-Desktop note.txt");
        const auto original = entry->position();
        const auto press = entry->mapToScene(QPointF(50, 20)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, press);
        QCOMPARE(items->selectionCount(), 1);
        QTest::mouseMove(&view, press + QPoint(-220, 100), 40);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, press + QPoint(-220, 100));
        QTRY_COMPARE(entry->position(), original + QPointF(-220, 100));
        QVERIFY(QFileInfo(fixture.path() + "/profile/desktop.json").isFile());
        DesktopItems restored(fixture.path(), fixture.path() + "/profile");
        for (int row = 0; row < restored.rowCount(); ++row)
            if (restored.data(restored.index(row), DesktopItems::NameRole) == "Desktop note.txt")
                QCOMPARE(QPointF(restored.data(restored.index(row), DesktopItems::XRole).toInt(),
                                 restored.data(restored.index(row), DesktopItems::YRole).toInt()),
                         entry->position());
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(300, 500));
        QCOMPARE(items->selectionCount(), 0);
        QVERIFY(manager.desktopActive());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/desktop-files.png"));
    }
    void shellLoadsAndMenusTrackPointer()
    {
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(
            view.status() != QQuickView::Error,
            qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject();
        QVERIFY(root);
        QCOMPARE(root->property("openMenu").toInt(), -1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(25, 13));
        QCOMPARE(root->property("openMenu").toInt(), 0);
        QTest::mouseMove(&view, QPoint(70, 13));
        QTRY_COMPARE(root->property("openMenu").toInt(), 1);
        QTest::mouseMove(&view, QPoint(72, 13));
        QCOMPARE(root->property("openMenu").toInt(), 1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(500, 500));
        QCOMPARE(root->property("openMenu").toInt(), -1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(70, 13));
        QCOMPARE(root->property("openMenu").toInt(), 1);
        manager.hideApplication();
        QCOMPARE(root->property("openMenu").toInt(), -1);
        QVERIFY(manager.activateDesktop());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        auto image = view.grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QCoreApplication::applicationDirPath() + "/artifacts/desktop.png"));
        // The manager context must outlive all QML objects that refer to it.
        view.setSource({});
    }
    void applicationMenuMatchesMacOS9Structure()
    {
        // docs/historical-fidelity-review.md F01/F03: glyph + name title without an
        // arrow, a divider from the clock, accent highlight, Hide/Hide Others/Show All.
        QTemporaryDir fixture;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject();
        auto *title = visualItem(view.contentItem(), "application-menu");
        QVERIFY(title && visualItem(view.contentItem(), "application-menu-divider"));
        QVERIFY(!view.grabWindow().isNull());
        // 9.2.2's etched strip: solid lines, not dots alone (psn_tch, 2026-10-22).
        {
            auto *divider = visualItem(view.contentItem(), "application-menu-divider");
            const auto shot = view.grabWindow();
            const auto ratio = shot.devicePixelRatio();
            const auto column = [&](const char *name)
            {
                auto *line = visualItem(view.contentItem(), name);
                const auto at = line->mapToScene(QPointF(0.5, 0));
                QSet<QRgb> colours;
                for (int y = int(at.y()) + 1; y < int(at.y() + line->height()) - 1; ++y)
                    colours.insert(shot.pixel(int(at.x() * ratio), int(y * ratio)) & 0xffffff);
                return colours;
            };
            QVERIFY(divider->x() + divider->width() == title->x());
            QCOMPARE(column("application-menu-divider-light"), QSet<QRgb>{0xffffff});
            QCOMPARE(column("application-menu-divider-shadow"), QSet<QRgb>{0x9c9c9c});
        }
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          title->mapToScene(QPointF(title->width() / 2, 13)).toPoint());
        QCOMPARE(root->property("openMenu").toInt(), root->property("barEntries").toList().size());
        QCOMPARE(title->property("color").value<QColor>(), root->property("menuAccent").value<QColor>());
        QVERIFY(root->property("menuAccent").value<QColor>() != QColor("#222222"));
        for (const auto *action : {"hide", "hide-others", "show-all"})
        {
            auto *item = visualItem(view.contentItem(), QString("desktop-command-") + action);
            QVERIFY2(item && item->isVisible(), action);
        }
        QVERIFY(visualItem(view.contentItem(), "desktop-command-separator"));
        QVERIFY(visualItem(view.contentItem(), "desktop-command-app"));
        auto *glyph = visualItem(view.contentItem(), "menu-glyph-Finder");
        QVERIFY(glyph && glyph->isVisible() && glyph->width() == 16);
        // Only Finder runs: nothing can be hidden or shown (as in the 9.2 reference).
        QCOMPARE(manager.applications().size(), 1);
        QVERIFY(!manager.applications().first().toMap().value("hidden").toBool());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/application-menu.png"));
        view.setSource({});
    }
    void desktopMenusFollowMacOS9LayoutAndEmptyTrashConfirms()
    {
        // docs/historical-fidelity-review.md F07/F08: Finder desktop menu bar with a
        // Window menu; Special > Empty Trash… asks first, Cancel being the default.
        QTemporaryDir fixture;
        const auto trash = fixture.path() + "/profile/data/Trash";
        QVERIFY(QDir().mkpath(trash + "/files/Old Folder") && QDir().mkpath(trash + "/info"));
        QVERIFY(QDir().mkpath(fixture.path() + "/outside"));
        {
            QFile item(trash + "/files/Old Folder/note.txt");
            QVERIFY(item.open(QIODevice::WriteOnly) && item.write("old") == 3);
        }
        QVERIFY(QFile::link(fixture.path() + "/outside", trash + "/files/Old Folder/link"));
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject();
        QStringList labels;
        for (const auto &entry : root->property("barEntries").toList())
            labels << entry.toMap().value("label").toString();
        QCOMPARE(labels, (QStringList{"◆", "File", "Edit", "View", "Window", "Special", "Help"}));
        const auto click = [&](const QString &name)
        {
            if (view.grabWindow().isNull())
                return false;
            auto *item = visualItem(view.contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled())
                return false;
            item->parentItem()->ensurePolished();
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        QVERIFY(click("bar-entry-0"));
        for (const auto *action : {"about", "applications", "control-panels", "puzzle", "recent-applications", "terminal"})
            QVERIFY2(visualItem(view.contentItem(), QString("desktop-command-") + action), action);
        QVERIFY(click("desktop-command-control-panels"));
        QTRY_VERIFY(visualItem(view.contentItem(), "desktop-command-desktop-picture"));
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/apple-menu.png"));
        QVERIFY(click("desktop-command-recent-applications"));
        QTRY_VERIFY(visualItem(view.contentItem(), "desktop-command-none"));
        // With a menu open, moving onto another title already switches to it;
        // dismiss first so each click opens exactly one menu.
        const auto dismiss = [&] { QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(600, 400)); };
        dismiss();
        QVERIFY(click("bar-entry-4")); // Window: no Finder windows in this preview
        QTRY_VERIFY(visualItem(view.contentItem(), "desktop-command-none"));
        dismiss();
        QVERIFY(click("bar-entry-5")); // Special
        // A preview has Log Out but never Sleep, Restart or Shut Down (the host's power).
        QTRY_VERIFY(visualItem(view.contentItem(), "desktop-command-quit"));
        for (const auto *action : {"sleep", "restart", "shut-down"})
            QVERIFY2(!visualItem(view.contentItem(), QString("desktop-command-") + action), action);
        QVERIFY(click("desktop-command-empty-trash"));
        auto *dialog = root->findChild<QObject *>("empty-trash-dialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(dialog->property("message").toString().startsWith("The Trash contains 1 item. It uses "));
        QTest::keyClick(&view, Qt::Key_Return); // Cancel is the default for a destructive command.
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QVERIFY(QFileInfo::exists(trash + "/files/Old Folder/note.txt"));
        QVERIFY(click("bar-entry-5"));
        QVERIFY(click("desktop-command-empty-trash"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(click("empty-trash-dialog-accept"));
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QTRY_VERIFY(!items->busy());
        QVERIFY(!QFileInfo::exists(trash + "/files/Old Folder"));
        QVERIFY(QFileInfo(fixture.path() + "/outside").isDir()); // The link was not followed.
        QVERIFY(QFileInfo(trash + "/files").isDir());
        QVERIFY(click("bar-entry-5"));
        QVERIFY(click("desktop-command-empty-trash"));
        QTRY_COMPARE(root->property("statusMessage").toString(), QString("The Trash is empty."));
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        view.setSource({});
    }
    void aboutThisComputerShowsTheBuiltInMemory()
    {
        // /proc/meminfo reports a size of 0; it must still be read (it said "Unknown", 2026-10-23).
        QTemporaryDir fixture;
        QQuickView view;
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        const auto about = manager.aboutThisComputer();
        QVERIFY2(QRegularExpression("Built-in Memory: [0-9]").match(about).hasMatch(), qPrintable(about));
    }
    void previewDoesNotLaunchHostPrograms()
    {
        QTemporaryDir fixture;
        QQuickView view;
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        manager.launchTerminal();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(manager.rowCount(), 0);
    }
    void exportedMenuCanReplaceItsItems()
    {
        QTemporaryDir fixture;
        QQuickView view;
        NativeManager manager(nullptr, &view, fixture.path(), fixture.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nimport QtQuick.Controls\nMenu {}", QUrl("test:menu"));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
        QScopedPointer<QObject> menu(component.create());
        QVERIFY(menu);
        QVERIFY(menu->setProperty("parent", QVariant::fromValue(view.rootObject())));
        auto entry = [](int id, QString label, QVariantList children = {})
        {
            return QVariantMap{{"id", id},         {"label", label},      {"visible", true},
                               {"enabled", true},  {"separator", false},  {"checkable", false},
                               {"checked", false}, {"children", children}};
        };
        QVariantList items{entry(1, "First"), entry(2, "Nested", {entry(3, "Child")})};
        QVERIFY(QMetaObject::invokeMethod(
            view.rootObject(), "populate", Q_ARG(QVariant, QVariant::fromValue(menu.data())),
            Q_ARG(QVariant, items), Q_ARG(QVariant, 0), Q_ARG(QVariant, 1)));
        QCOMPARE(menu->property("count").toInt(), 2);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "clearEntries",
                                          Q_ARG(QVariant, QVariant::fromValue(menu.data()))));
        QCOMPARE(menu->property("count").toInt(), 0);
        QVariantList replacement{entry(4, "Replacement")};
        QVERIFY(QMetaObject::invokeMethod(
            view.rootObject(), "populate", Q_ARG(QVariant, QVariant::fromValue(menu.data())),
            Q_ARG(QVariant, replacement), Q_ARG(QVariant, 0), Q_ARG(QVariant, 2)));
        QCOMPARE(menu->property("count").toInt(), 1);
    }
};
QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
