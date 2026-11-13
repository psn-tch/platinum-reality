#include <QDateTime>
#include "applications/application_catalog.hpp"
#include "desktop/appearance_settings.hpp"
#include "desktop/alert_sounds.hpp"
#include "desktop/battery_status.hpp"
#include "desktop/interface_sounds.hpp"
#include "desktop/command_keys.hpp"
#include "desktop/computer_info.hpp"
#include "desktop/audio_service.hpp"
#include "desktop/desktop_items.hpp"
#include "desktop/display_layout.hpp"
#include "desktop/display_topology_feed.hpp"
#include "desktop/drop_receiver_window.hpp"
#include "desktop/energy_settings.hpp"
#include "desktop/input_settings.hpp"
#include "desktop/manager.hpp"
#include "desktop/software_update.hpp"
#include "desktop/window_move_outline.hpp"
#include "desktop/printer_selector.hpp"
#include "desktop/camera_switch.hpp"
#include "desktop/system_sounds.hpp"
#include "desktop/keychain_service.hpp"
#include "desktop/sticky_notes.hpp"
#include "desktop/note_pad.hpp"
#include "desktop/scrapbook.hpp"
#include "desktop/key_caps.hpp"
#include "desktop/surface_client.hpp"
#include "desktop/screenshot_service.hpp"
#include <QBuffer>
#include <QJSValue>
#include <QMimeData>
#include <QStyleHints>
#include <QClipboard>
#include "desktop/system_controls.hpp"
#include "finder/file_drag.hpp"
#include "native_keys.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QImage>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <memory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QProcess>
#include <QQmlComponent>
#include <QRegularExpression>
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
    void iconReadsDoNotStartDiscoveryBeforeApplicationsExist()
    {
        QTemporaryDir root;QQuickView view;view.resize(1120,760);
        const auto apps=root.filePath("applications");
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{},{apps});
        auto *catalog=qobject_cast<ApplicationCatalog*>(manager.applicationCatalog());QVERIFY(catalog);
        QSignalSpy refreshed(catalog,&ApplicationCatalog::refreshed);
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clear=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        manager.applicationIcon("org.frankenstein.Finder");
        manager.applicationIcon("org.example.Late");
        manager.applications();
        QVERIFY(!catalog->busy());QVERIFY(!catalog->ready());QCOMPARE(refreshed.count(),0);
        // Native qualification and late application installation both populate the
        // directory after the shell exists. Only the explicit refresh owns discovery.
        QVERIFY(QDir().mkpath(apps));
        QFile entry(apps+"/org.example.Late.desktop");QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Late catalog fixture\nExec=/usr/bin/true --owned\n");
        entry.close();
        catalog->setQuery("Late catalog fixture");catalog->refresh();
        QTRY_VERIFY(catalog->ready() && !catalog->busy());QCOMPARE(refreshed.count(),1);
        QCOMPARE(catalog->count(),1);
        QCOMPARE(catalog->item(0).value("id").toString(),QString("org.example.Late.desktop"));
        const auto plan=catalog->launchPlan("org.example.Late.desktop",{},QProcessEnvironment::systemEnvironment());
        QVERIFY2(bool(plan),qPrintable(plan.error));
        QCOMPARE(plan.program,QString("/usr/bin/true"));QCOMPARE(plan.arguments,QStringList{"--owned"});
    }
    void applicationIconsUseIdentityPrivateArtworkAndRefresh()
    {
        QTemporaryDir root;
        const auto previousData = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", root.filePath("data").toLocal8Bit());
        const auto restore = qScopeGuard([&] {
            if (previousData.isNull()) qunsetenv("XDG_DATA_HOME");
            else qputenv("XDG_DATA_HOME", previousData);
        });
        const auto apps = root.filePath("apps");
        const auto icons = root.filePath("data/platinum-reality/assets/icons");
        QVERIFY(QDir().mkpath(apps));QVERIFY(QDir().mkpath(icons));
        const auto paint = root.filePath("paint.png");
        QImage image(16,16,QImage::Format_ARGB32);image.fill(Qt::red);QVERIFY(image.save(paint));
        image.fill(Qt::green);QVERIFY(image.save(icons+"/finder-16.png"));
        QFile entry(apps+"/org.example.Paint.desktop");QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Paint\nExec=/usr/bin/true\nIcon="+paint.toUtf8()+"\n");entry.close();
        QQuickView view;
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{},{apps});
        auto *catalog=qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());QVERIFY(catalog);
        catalog->refresh();QTRY_VERIFY(catalog->ready());
        const auto decoded=[](QUrl url) {
            return QImage::fromData(QByteArray::fromBase64(url.toString().section(',',1).toLatin1()));
        };
        auto native=manager.applicationIcon("org.example.Paint");
        QVERIFY(native.scheme()=="data");QCOMPARE(decoded(native).pixelColor(8,8),QColor(Qt::red));
        QCOMPARE(decoded(manager.applicationIcon("org.example.Paint.desktop")).pixelColor(8,8),QColor(Qt::red));
        QVERIFY(manager.applicationIcon("Paint").isEmpty());
        QVERIFY(manager.applicationIcon("org.example.Missing").isEmpty());
        QCOMPARE(decoded(manager.applications().first().toMap().value("icon").toUrl()).pixelColor(8,8),QColor(Qt::green));
        image.fill(Qt::blue);QVERIFY(image.save(paint));
        QCOMPARE(manager.applicationIcon("org.example.Paint"),native); // Stable until an explicit refresh.
        manager.refreshApplicationIcons();
        QCOMPARE(decoded(manager.applicationIcon("org.example.Paint")).pixelColor(8,8),QColor(Qt::blue));
        image.fill(Qt::yellow);QVERIFY(image.save(paint));
        QSignalSpy refreshed(catalog,&ApplicationCatalog::refreshed);
        catalog->refresh();QTRY_COMPARE(refreshed.count(),1);
        QCOMPARE(decoded(manager.applicationIcon("org.example.Paint")).pixelColor(8,8),QColor(Qt::yellow));
    }

    void switcherUsesApplicationIconsAndKeepsFallbackAndTargetIdentity()
    {
        QTemporaryDir root;QQuickView view;view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clear=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        QImage image(16,16,QImage::Format_ARGB32);image.fill(Qt::red);
        QByteArray bytes;QBuffer buffer(&bytes);QVERIFY(buffer.open(QIODevice::WriteOnly));QVERIFY(image.save(&buffer,"PNG"));
        const QUrl source("data:image/png;base64,"+QString::fromLatin1(bytes.toBase64()));
        auto *switcher=visualItem(view.contentItem(),"application-switcher");QVERIFY(switcher);
        QVariantList apps{
            QVariantMap{{"id",45},{"name","Paint"},{"icon",source},{"active",true},{"hidden",false}},
            QVariantMap{{"id",99},{"name","No Icon"},{"icon",QUrl()},{"active",false},{"hidden",true}}
        };
        QVERIFY(switcher->setProperty("applications",apps));
        auto *settings=qobject_cast<AppearanceSettings*>(manager.appearance());QVERIFY(settings);
        QVERIFY(settings->setSwitcher(true,80,120));QTRY_VERIFY(switcher->isVisible());
        QCOMPARE(switcher->size(),QSizeF(158,68));
        QCOMPARE(switcher->property("rowHeight").toInt(),24);
        auto *icon=visualItem(switcher,"application-switcher-icon-Paint");QVERIFY(icon);
        QTRY_VERIFY(!icon->property("useFallback").toBool());
        QCOMPARE(icon->size(),QSizeF(16,16));
        QCOMPARE(icon->property("source").toUrl(),source);
        auto *fallback=visualItem(switcher,"application-switcher-icon-No Icon");QVERIFY(fallback);
        QVERIFY(fallback->property("useFallback").toBool());QCOMPARE(fallback->opacity(),qreal(.5));
        auto *active=visualItem(switcher,"application-switcher-Paint");QVERIFY(active);
        QVERIFY(active->property("down").toBool());
        const auto capture=view.grabWindow();QVERIFY(!capture.isNull());
        QVERIFY(capture.save(QCoreApplication::applicationDirPath()+"/artifacts/application-switcher-icons.png"));
        // The software grab can contain physical pixels with DPR metadata 1.
        const qreal captureScale=qreal(capture.width())/view.width();
        const auto at=icon->mapToScene(QPointF(8,8))*captureScale;
        QCOMPARE(capture.pixelColor(at.toPoint()),QColor(Qt::red));
        // Independently sampled reference patch x349..363/y179..190, relative
        // to palette origin (336,177). Excludes artwork, title and controls.
        const QStringList titlePatch{
            "...............", "...............", ".W..W..W..W..W.",
            "..D..D..D..D..D", "...............", ".W..W..W..W..W.",
            "..D..D..D..D..D", "...............", ".W..W..W..W..W.",
            "..D..D..D..D..D", "...............", "..............."};
        for(int y=0;y<titlePatch.size();++y) {
            for(int x=0;x<titlePatch[y].size();++x) {
                const auto mark=titlePatch[y][x];
                const QColor expected=mark=='W'?QColor(Qt::white):mark=='D'?QColor("#777"):QColor("#ccc");
                const auto pixel=switcher->mapToScene(QPointF(13+x+.5,2+y+.5))*captureScale;
                QCOMPARE(capture.pixelColor(QPoint(qFloor(pixel.x()),qFloor(pixel.y()))),expected);
            }
        }
        QSignalSpy chosen(switcher,SIGNAL(chosen(QVariant)));QVERIFY(chosen.isValid());
        auto *row=visualItem(switcher,"application-switcher-No Icon");QVERIFY(row);
        QTest::mouseClick(&view,Qt::LeftButton,{},row->mapToScene({row->width()/2,row->height()/2}).toPoint());
        QTRY_COMPARE(chosen.count(),1);QCOMPARE(chosen.first().first().toInt(),99);
        // An unreadable image restores the independent glyph rather than leaving an empty row.
        apps[0]=QVariantMap{{"id",45},{"name","Paint"},{"icon",QUrl("file:///nonexistent/platinum-icon.png")},{"active",true}};
        QVERIFY(switcher->setProperty("applications",apps));
        icon=visualItem(switcher,"application-switcher-icon-Paint");QVERIFY(icon);
        QTRY_VERIFY(icon->property("useFallback").toBool());
    }
    void menuBarUsesMeasuredHeightAndExplicitKeyboardAccess()
    {
        QTemporaryDir root;QQuickView view;view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *desktop=view.rootObject();auto *bar=visualItem(desktop,"desktop-menu-bar");
        QVERIFY(bar);QCOMPARE(bar->height(),qreal(20));
        QVariant menuRoots;
        QVariant provider=QVariantList{QVariantMap{{"id",77},{"label","File"},{"visible",true},{"enabled",true},
            {"children",QVariantList{QVariantMap{{"id",701},{"label","Provider Save"}}}}}};
        QVERIFY(QMetaObject::invokeMethod(desktop,"applicationMenuRoots",Q_RETURN_ARG(QVariant,menuRoots),
            Q_ARG(QVariant,provider),Q_ARG(QVariant,false)));
        QCOMPARE(menuRoots.toList().size(),1);
        QCOMPARE(menuRoots.toList().first().toMap().value("kind").toString(),QString("export"));
        QCOMPARE(menuRoots.toList().first().toMap().value("id").toInt(),77);
        QCOMPARE(menuRoots.toList().first().toMap().value("children").toList().first().toMap().value("id").toInt(),701);
        provider=QVariantList{QVariantMap{{"id",88},{"label","Actions"},{"visible",true},{"enabled",true}}};
        QVERIFY(QMetaObject::invokeMethod(desktop,"applicationMenuRoots",Q_RETURN_ARG(QVariant,menuRoots),
            Q_ARG(QVariant,provider),Q_ARG(QVariant,false)));
        QCOMPARE(menuRoots.toList().size(),2);
        QCOMPARE(menuRoots.toList().first().toMap().value("kind").toString(),QString("app-file"));
        QCOMPARE(menuRoots.toList().at(1).toMap().value("id").toInt(),88);
        auto *file=visualItem(desktop,"bar-entry-1");QVERIFY(file);
        QCOMPARE(file->x(),qreal(29)); // Apple starts at row x8 and occupies 29px.
        QTest::mouseClick(&view,Qt::LeftButton,{},file->mapToScene({file->width()/2,8}).toPoint());
        QTRY_VERIFY(manager.menuOpen());
        auto *popup=visualItem(desktop,"desktop-menu-popup");QVERIFY(popup);
        QCOMPARE(popup->y(),qreal(19));
        const auto menuEvidence=qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        if(!menuEvidence.isEmpty()){QDir().mkpath(menuEvidence);QVERIFY(view.grabWindow().save(menuEvidence+"/menu-file-open.png"));}
        // Pointer-open menus keep the measured sticky-menu key-to-dismiss behavior.
        QTest::keyClick(&view,Qt::Key_Down);QTRY_VERIFY(!manager.menuOpen());
        QTest::keyClick(&view,Qt::Key_F10);QTRY_VERIFY(manager.menuOpen());
        QVERIFY(desktop->property("menuKeyboardMode").toBool());
        QTest::keyClick(&view,Qt::Key_Down);QTRY_COMPARE(desktop->property("menuSelection").toInt(),0);
        QTest::keyClick(&view,Qt::Key_Right);QTRY_COMPARE(desktop->property("openMenu").toInt(),1);
        QTest::keyClick(&view,Qt::Key_Down);QTRY_COMPARE(desktop->property("menuSelection").toInt(),0);
        QTest::keyClick(&view,Qt::Key_End);
        QVERIFY(desktop->property("menuSelection").toInt()>0);
        QTest::keyClick(&view,Qt::Key_Escape);QTRY_VERIFY(!manager.menuOpen());
        QTest::keyClick(&view,Qt::Key_F10);QTRY_VERIFY(manager.menuOpen());
        QTest::keyClick(&view,Qt::Key_Down);QTest::keyClick(&view,Qt::Key_Return);
        auto *about=visualItem(desktop,"about-computer-window");QVERIFY(about);
        QTRY_VERIFY(about->isVisible());
        QTRY_VERIFY(about->property("front").toBool());
        QVariant entries;
        QVERIFY(QMetaObject::invokeMethod(desktop,"shellMenuEntries",Q_RETURN_ARG(QVariant,entries),Q_ARG(QVariant,1)));
        QCOMPARE(entries.toList().size(),1);
        QCOMPARE(entries.toList().first().toMap().value("action").toString(),QString("close-active-window"));
        QVERIFY(entries.toList().first().toMap().value("enabled").toBool());
        QTest::keyClick(&view,Qt::Key_F10);QTRY_COMPARE(desktop->property("openMenu").toInt(),0);
        QTRY_VERIFY(desktop->property("menuKeyboardMode").toBool());
        QTest::keyClick(&view,Qt::Key_Right);QTRY_COMPARE(desktop->property("openMenu").toInt(),1);
        QTest::keyClick(&view,Qt::Key_Down);QTRY_COMPARE(desktop->property("menuSelection").toInt(),0);
        QVariant closeEntries;
        QVERIFY(QMetaObject::invokeMethod(desktop,"shellMenuEntries",Q_RETURN_ARG(QVariant,closeEntries),Q_ARG(QVariant,1)));
        QCOMPARE(closeEntries.toList().size(),1);
        QTest::keyClick(&view,Qt::Key_Return);
        QTRY_VERIFY(!about->isVisible()); // Menu blink retains the accessory object target.
        QTest::keyClick(&view,Qt::Key_F10);QTRY_VERIFY(manager.menuOpen());
        QTest::keyClick(&view,Qt::Key_Down);QTRY_COMPARE(desktop->property("menuSelection").toInt(),0);
        QTest::keyClick(&view,Qt::Key_Return);
        QTRY_VERIFY(about->isVisible());QTRY_VERIFY(about->property("front").toBool());
        QTest::keyClick(&view,Qt::Key_W,Qt::MetaModifier);
        QTRY_VERIFY(!about->isVisible());
        const auto evidence=qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        if(!evidence.isEmpty()){QDir().mkpath(evidence);QVERIFY(view.grabWindow().save(evidence+"/menu-bar-measured.png"));}
    }
    // Apple Menu Options (9.2.2's, captured 2026-11-12): Submenus On or Off, and how many recent
    // applications to remember, or none. The Apple menu follows at once; the settings persist.
    void appleMenuOptionsShapeTheAppleMenu()
    {
        QTemporaryDir root;
        QDir().mkpath(root.filePath("profile"));
        {
            QFile recent(root.filePath("profile/recent-applications.json"));
            QVERIFY(recent.open(QIODevice::WriteOnly));
            QJsonArray list;
            for (const auto *name : {"Alpha", "Beta", "Gamma", "Delta", "Epsilon"})
                list.append(QJsonObject{{"id", QString(name).toLower() + ".desktop"}, {"name", name}});
            recent.write(QJsonDocument(list).toJson());
        }
        QQuickView view; view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *desktop = view.rootObject();
        auto *appearance = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(appearance);
        const auto apple = [&] {
            QVariant entries;
            QMetaObject::invokeMethod(desktop, "shellMenuEntries", Q_RETURN_ARG(QVariant, entries), Q_ARG(QVariant, 0));
            QVariantMap byText;
            QStringList order;
            for (const auto &value : entries.toList()) {
                const auto entry = value.toMap();
                if (entry.value("separator").toBool()) continue;
                byText.insert(entry.value("text").toString(), entry);
                order << entry.value("text").toString();
            }
            byText.insert("__order", order);
            return byText;
        };
        const auto set = [&](const QString &key, const QVariant &value) {
            bool accepted = false;
            QMetaObject::invokeMethod(appearance, "setAppleMenuOption", Q_RETURN_ARG(bool, accepted),
                                      Q_ARG(QString, key), Q_ARG(QVariant, value));
            return accepted;
        };
        // Defaults: submenus, ten remembered (five here), Recent Applications between Stickies and Terminal.
        auto menu = apple();
        QCOMPARE(menu.value("Recent Applications").toMap().value("children").toList().size(), 5);
        QVERIFY(!menu.value("Control Panels").toMap().value("children").toList().isEmpty());
        auto order = menu.value("__order").toStringList();
        QCOMPARE(order.indexOf("Recent Applications"), order.indexOf("Stickies") + 1);
        QCOMPARE(order.indexOf("Terminal"), order.indexOf("Recent Applications") + 1);
        QVERIFY(menu.value("Control Panels").toMap().value("children").toString().isEmpty());
        bool listed = false;
        for (const auto &child : menu.value("Control Panels").toMap().value("children").toList())
            listed = listed || child.toMap().value("text").toString() == "Apple Menu Options…";
        QVERIFY(listed);
        // The panel: its controls set the options.
        QVERIFY(QMetaObject::invokeMethod(desktop, "dispatchShellCommand", Q_ARG(QVariant, QVariant("apple-menu-options")), Q_ARG(QVariant, QVariant(0))));
        auto *panel = desktop->findChild<QObject *>("apple-menu-options-panel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        const auto click = [&](QQuickItem *target) {
            QTest::mouseClick(&view, Qt::LeftButton, {}, target->mapToScene(QPointF(8, target->height() / 2)).toPoint());
        };
        QTRY_VERIFY(item("apple-menu-submenus-off") && item("apple-menu-submenus-off")->isVisible());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/apple-menu-options.png"));
        click(item("apple-menu-submenus-off"));
        QTRY_COMPARE(appearance->appleMenu().value("submenus").toBool(), false);
        QVERIFY(item("apple-menu-submenus-off")->property("checked").toBool());
        QVERIFY(!item("apple-menu-submenus-on")->property("checked").toBool());
        menu = apple();
        QVERIFY(menu.value("Control Panels").toMap().value("children").toList().isEmpty());
        QCOMPARE(menu.value("Control Panels").toMap().value("action").toString(), QString("control-panels-window"));
        QCOMPARE(menu.value("Recent Applications").toMap().value("action").toString(), QString("applications"));
        // How many: two.
        auto *field = item("apple-menu-recent-applications");
        QVERIFY(field);
        field->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(field, "selectAll"));
        QTest::keyClick(&view, Qt::Key_2);
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(appearance->appleMenu().value("recentApplications").toInt(), 2);
        QVERIFY(set("submenus", true));
        menu = apple();
        const auto recent = menu.value("Recent Applications").toMap().value("children").toList();
        QCOMPARE(recent.size(), 2);
        QCOMPARE(recent.at(0).toMap().value("text").toString(), QString("Alpha"));
        // Remembering off: no Recent Applications at all.
        click(item("apple-menu-remember"));
        QTRY_COMPARE(appearance->appleMenu().value("rememberRecent").toBool(), false);
        QVERIFY(!apple().contains("Recent Applications"));
        QVERIFY(!item("apple-menu-recent-applications")->isEnabled());
        // Kept in appearance.json.
        QFile saved(root.filePath("profile/appearance.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto options = QJsonDocument::fromJson(saved.readAll()).object().value("appleMenu").toObject();
        QCOMPARE(options.value("submenus").toBool(true), true);
        QCOMPARE(options.value("rememberRecent").toBool(true), false);
        QCOMPARE(options.value("recentApplications").toInt(), 2);
        // Out-of-range values are refused, on a second instance (an error alert would block this view).
        AppearanceSettings other(root.filePath("other-profile"));
        QVERIFY(!other.setAppleMenuOption("recentApplications", 0));
        QVERIFY(!other.setAppleMenuOption("recentApplications", 100));
        QVERIFY(!other.setAppleMenuOption("submenus", 1));
        QVERIFY(!other.setAppleMenuOption("servers", 5));
        QVERIFY(other.setAppleMenuOption("recentApplications", 99));
    }
    // File Exchange (after 9.2.2's PC Exchange, captured 2026-11-12): which application opens each kind
    // of document from the Finder and the desktop. Change… offers the applications that say they open it.
    void fileExchangeChoosesWhichApplicationOpensEachKind()
    {
        QTemporaryDir root;
        const auto apps = root.filePath("applications");
        QDir().mkpath(apps);
        const auto entry = [&](const QString &file, const QString &body) {
            QFile f(apps + "/" + file); QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(("[Desktop Entry]\nType=Application\n" + body).toUtf8());
        };
        entry("viewer.desktop", "Name=Picture Viewer\nExec=/usr/bin/true %f\nMimeType=image/png;image/jpeg;\n");
        entry("painter.desktop", "Name=Painter\nExec=/usr/bin/true %U\nMimeType=image/png;\n");
        entry("clock.desktop", "Name=Clock\nExec=/usr/bin/true\nMimeType=image/png;\n"); // Opens no documents.
        QQuickView view; view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("profile"), {}, {apps});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *desktop = view.rootObject();
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(catalog);
        QVERIFY(QMetaObject::invokeMethod(desktop, "dispatchShellCommand", Q_ARG(QVariant, QVariant("file-exchange")), Q_ARG(QVariant, QVariant(0))));
        auto *panel = desktop->findChild<QObject *>("file-exchange-panel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(catalog->ready(), 10000);
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        const auto click = [&](QQuickItem *target) {
            QTest::mouseClick(&view, Qt::LeftButton, {}, target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
        };
        // The common kinds are listed; text opens in the editor until something else is chosen.
        QQuickItem *png = nullptr;
        QTRY_VERIFY((png = item("file-exchange-png")) && png->isVisible());
        QVERIFY(item("file-exchange-txt"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/file-exchange.png"));
        click(png);
        QTRY_COMPARE(panel->property("selectedType").toString(), QString("image/png"));
        QVERIFY(!item("file-exchange-remove")->isEnabled()); // Nothing chosen yet.
        click(item("file-exchange-change"));
        auto *chooser = panel->findChild<QObject *>("file-exchange-chooser");
        QVERIFY(chooser);
        QStringList offered;
        for (const auto &choice : chooser->property("choices").toList()) offered << choice.toMap().value("name").toString();
        QCOMPARE(offered, (QStringList{"Painter", "Picture Viewer"})); // Not Clock: it opens no documents.
        QTest::keyClick(&view, Qt::Key_Escape);
        QVERIFY(QMetaObject::invokeMethod(panel, "choose", Q_ARG(QVariant, QVariant("image/png")), Q_ARG(QVariant, QVariant("viewer.desktop"))));
        QCOMPARE(catalog->preferredApplication(root.filePath("photo.png")), QString("viewer.desktop"));
        QFile saved(root.filePath("profile/associations.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(saved.readAll()).object().value("types").toObject().value("image/png").toString(), QString("viewer.desktop"));
        bool shown = false;
        for (const auto &row : catalog->fileTypes())
            if (row.toMap().value("type") == "image/png") shown = row.toMap().value("applicationName").toString() == "Picture Viewer";
        QVERIFY(shown);
        QTRY_VERIFY(item("file-exchange-remove")->isEnabled());
        click(item("file-exchange-remove"));
        QTRY_VERIFY(catalog->preferredApplication(root.filePath("photo.png")).isEmpty());
        // Add…: an extension names a kind; an unknown one says so.
        click(item("file-exchange-add"));
        auto *field = item("file-exchange-extension");
        QTRY_VERIFY(field && field->isVisible());
        field->setProperty("text", "zzzq");
        click(item("file-exchange-add-kind"));
        QTRY_VERIFY(item("file-exchange-message")->property("text").toString().contains("zzzq"));
        QCOMPARE(catalog->fileTypeFor(".WebP").value("type").toString(), QString("image/webp"));
        field->setProperty("text", "webp");
        click(item("file-exchange-add-kind"));
        QTRY_COMPARE(panel->property("selectedType").toString(), QString("image/webp"));
        QVERIFY(item("file-exchange-webp"));
        QTest::keyClick(&view, Qt::Key_Escape);
        // Refused: an application that opens no documents, or a kind that isn't one.
        QVERIFY(!catalog->setFileTypeApplication("image/png", "clock.desktop"));
        QVERIFY(!catalog->setFileTypeApplication("not/a-type", "viewer.desktop"));
    }
    void menuBarSizeEnlargesBarMenusAndStripTogether()
    {
        QTemporaryDir root;QQuickView view;view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *desktop=view.rootObject();auto *bar=visualItem(desktop,"desktop-menu-bar");QVERIFY(bar);
        auto *appearance=manager.appearance();
        QCOMPARE(manager.menuBarHeight(),20);QCOMPARE(bar->scale(),qreal(1));
        // Every Apple menu item but About This Computer has an icon (Lily Hewitt 2026-11-09).
        QVariant apple;
        QVERIFY(QMetaObject::invokeMethod(desktop,"shellMenuEntries",Q_RETURN_ARG(QVariant,apple),Q_ARG(QVariant,0)));
        for(const auto &value:apple.toList()) {
            const auto entry=value.toMap();
            if(entry.value("separator").toBool())continue;
            QCOMPARE(entry.value("glyph").toString().isEmpty(),entry.value("action").toString()=="about");
            for(const auto &child:entry.value("children").toList())
                if(child.toMap().value("action").toString().size()>0 && !child.toMap().value("separator").toBool() &&
                   entry.value("action").toString()=="control-panels")
                    QVERIFY(!child.toMap().value("glyph").toString().isEmpty());
        }
        const auto evidence=qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        auto *strip=visualItem(desktop,"control-strip");QVERIFY(strip);
        for(const int size:{100,125}) {
            bool accepted=false;
            QVERIFY(QMetaObject::invokeMethod(appearance,"setGeneralOption",Q_RETURN_ARG(bool,accepted),
                                              Q_ARG(QString,"barSize"),Q_ARG(QVariant,size)));
            QVERIFY(accepted);
            const qreal zoom=size/100.0;
            QCOMPARE(manager.menuBarHeight(),qRound(20*zoom));
            QCOMPARE(bar->scale(),zoom);QCOMPARE(strip->scale(),zoom);
            QCOMPARE(bar->mapRectToScene(QRectF(0,0,bar->width(),bar->height())).width(),qreal(view.width()));
            // The strip stays on screen and below the bar.
            const auto stripRect=strip->mapRectToScene(QRectF(0,0,strip->width(),strip->height()));
            QVERIFY(stripRect.top()>=manager.menuBarHeight());QVERIFY(stripRect.bottom()<=view.height());
            auto *appleTitle=visualItem(desktop,"bar-entry-0");QVERIFY(appleTitle);
            QTest::mouseClick(&view,Qt::LeftButton,{},appleTitle->mapToScene({appleTitle->width()/2,8}).toPoint());
            QTRY_VERIFY(manager.menuOpen());
            auto *popup=visualItem(desktop,"desktop-menu-popup");QVERIFY(popup);
            QCOMPARE(popup->scale(),zoom);
            // The menu's top edge lies over the bar's black lower border.
            QCOMPARE(popup->y(),19*zoom);
            auto *panels=visualItem(desktop,"desktop-command-control-panels");QVERIFY(panels);
            QTest::mouseClick(&view,Qt::LeftButton,{},panels->mapToScene({panels->width()/2,panels->height()/2}).toPoint());
            auto *submenu=visualItem(desktop,"desktop-submenu");QVERIFY(submenu);
            QTRY_VERIFY(submenu->isVisible());
            const auto popupRect=popup->mapRectToScene(QRectF(0,0,popup->width(),popup->height()));
            QVERIFY(qAbs(submenu->x()-(popupRect.right()-4*zoom))<1);
            if(!evidence.isEmpty()){QDir().mkpath(evidence);
                QVERIFY(view.grabWindow().save(evidence+QString("/menu-bar-size-%1.png").arg(size)));}
            QTest::keyClick(&view,Qt::Key_Escape);QTRY_VERIFY(!manager.menuOpen());
        }
        // Only whole steps of five, 100-150 percent.
        for(const auto refused:{QVariant(95),QVariant(107),QVariant(155),QVariant(true),QVariant(112.5)}) {
            bool accepted=true;
            QVERIFY(QMetaObject::invokeMethod(appearance,"setGeneralOption",Q_RETURN_ARG(bool,accepted),
                                              Q_ARG(QString,"barSize"),Q_ARG(QVariant,refused)));
            QVERIFY(!accepted);
        }
    }
    void exportedSubmenuRowsLineUpWithCommands()
    {
        QTemporaryDir root;QQuickView view;view.resize(800,600);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *desktop=view.rootObject();
        auto entry=[](int id,QString label,QVariantList children={}){
            return QVariantMap{{"id",id},{"label",label},{"visible",true},{"enabled",true},{"separator",false},
                               {"checkable",false},{"checked",false},{"children",children}};};
        // Konsole's Edit menu: commands, then a submenu row (Lily Hewitt's photo, 2026-11-09), built
        // by the desktop's own menu factory as an application's menu is.
        auto *factory=qobject_cast<QQmlComponent *>(qmlContext(desktop)->objectForName("menuFactory"));
        QVERIFY(factory);
        QScopedPointer<QObject> popupOwner(factory->create(qmlContext(desktop)));
        auto *popup=popupOwner.data();QVERIFY(popup);
        QVERIFY(popup->setProperty("parent",QVariant::fromValue(view.contentItem())));
        const QVariantList items{entry(51,"Copy"),entry(52,"Copy Input To",{entry(53,"All Tabs")})};
        QVERIFY(QMetaObject::invokeMethod(desktop,"populate",Q_ARG(QVariant,QVariant::fromValue(popup)),
            Q_ARG(QVariant,items),Q_ARG(QVariant,0),Q_ARG(QVariant,1)));
        QVERIFY(QMetaObject::invokeMethod(popup,"open"));
        QTRY_VERIFY(popup->property("opened").toBool());
        QQuickItem *command=nullptr,*submenuRow=nullptr;
        QTRY_VERIFY((command=visualItem(view.contentItem(),"menu-command-51")));
        for(int i=0;i<popup->property("count").toInt();++i){
            QQuickItem *item=nullptr;
            QMetaObject::invokeMethod(popup,"itemAt",Q_RETURN_ARG(QQuickItem*,item),Q_ARG(int,i));
            if(item && item->property("subMenu").value<QObject *>())submenuRow=item;
        }
        QVERIFY(submenuRow);
        auto textX=[](QQuickItem *row){
            auto *text=row->property("contentItem").value<QQuickItem *>();
            return text->mapToScene({text->property("leftPadding").toReal(),0}).x();};
        QCOMPARE(textX(submenuRow),textX(command));
        QCOMPARE(submenuRow->height(),command->height());
    }
    void outlineStripsInvertCheckerPixelsAndRejectStaleSnapshots()
    {
        auto snapshot=std::make_shared<WindowMoveSnapshot>();
        snapshot->image=QImage(12,12,QImage::Format_RGB32);
        snapshot->image.fill(QColor("#63639c"));snapshot->generation=4;
        auto strip=windowMoveStrip(*snapshot,{2,3,6,2});
        QCOMPARE(strip.size(),QSize(6,2));
        QCOMPARE(strip.pixelColor(0,0),QColor("#9c9c63"));
        QCOMPARE(strip.pixelColor(1,0).alpha(),0);
        QCOMPARE(strip.pixelColor(0,1).alpha(),0);
        QCOMPARE(strip.pixelColor(1,1),QColor("#9c9c63"));
        auto clipped=windowMoveStrip(*snapshot,{-1,0,2,4});
        QCOMPARE(clipped.pixelColor(0,0).alpha(),0);
        WindowMoveOutlineProvider provider(snapshot);QSize size;
        QVERIFY(provider.requestImage("3/2/3/6/2",&size,{}).isNull());
        QVERIFY(provider.requestImage("4/0/0/500/500",&size,{}).isNull());
        QCOMPARE(provider.requestImage("4/2/3/6/2",&size,{}),strip);
        snapshot->scale=2;
        auto scaled=windowMoveStrip(*snapshot,{1,2,2,2});
        QCOMPARE(scaled.size(),QSize(4,4));
        QCOMPARE(scaled.pixelColor(0,0),QColor("#9c9c63"));
        QCOMPARE(scaled.pixelColor(1,1),QColor("#9c9c63"));
        QCOMPARE(scaled.pixelColor(2,0).alpha(),0);
        snapshot->image={};
        auto fallback=windowMoveStrip(*snapshot,{0,0,2,2});
        QCOMPARE(fallback.pixelColor(0,0),QColor(Qt::black));
        QCOMPARE(fallback.pixelColor(2,0),QColor(Qt::white));
    }
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(QCoreApplication::applicationDirPath()+"/artifacts"));
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
    void unboundCommandLettersNeverEditShellFields_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("fieldName");
        for (auto field : {std::pair{"applications", "application-search"},
                           {"keyboard", "keyboard-try"}, {"new-folder", "desktop-name-field"},
                           {"screenshot", "screenshot-path"}, {"desktop-picture", "picture-folder"},
                           {"desktop-picture", "picture-path"}})
            QTest::newRow(field.second) << QString(field.first) << QString(field.second);
    }
    void unboundCommandLettersNeverEditShellFields()
    {
        // Component counterpart to the real-keyboard native audit. Supply the
        // printable text carried by a real Meta+N event; commandKey() supplies
        // no text and cannot expose this Qt editor behavior. No layout/seat proof.
        QFETCH(QString, action);
        QFETCH(QString, fieldName);
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(),"dispatchShellCommand",
            Q_ARG(QVariant,QVariant(action)),Q_ARG(QVariant,QVariant(0))));
        auto *field=visualItem(view.contentItem(),fieldName);
        QVERIFY(field);
        QTRY_VERIFY(field->isVisible());
        field->setProperty("text",QString("unchanged fixture"));
        field->forceActiveFocus();
        QTRY_VERIFY(field->hasActiveFocus());
        nativeKeyEvent(&view,QEvent::KeyPress,Qt::Key_Meta,Qt::MetaModifier,125);
        nativeKeyEvent(&view,QEvent::KeyPress,Qt::Key_N,Qt::MetaModifier,49,QString("n"));
        nativeKeyEvent(&view,QEvent::KeyRelease,Qt::Key_N,Qt::MetaModifier,49);
        nativeKeyEvent(&view,QEvent::KeyRelease,Qt::Key_Meta,Qt::NoModifier,125);
        QTest::qWait(50);
        QCOMPARE(field->property("text").toString(),QString("unchanged fixture"));
    }
    void openControlPanelsAndAccessoriesAreListedAsApplications()
    {
        // Lily Hewitt, 2026-11-07: Control Panel didn't show in the Application Switcher. In 9.2.2 control
        // panels and desk accessories were applications, listed in the Application menu and switcher.
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto dispatch=[&](const QVariant &action,const QVariant &id=0){return QMetaObject::invokeMethod(
            view.rootObject(),"dispatchShellCommand",Q_ARG(QVariant,action),Q_ARG(QVariant,id));};
        const auto listed=[&]{
            QVariant result;
            QMetaObject::invokeMethod(view.rootObject(),"applicationsByName",Q_RETURN_ARG(QVariant,result));
            QVariantMap byName;
            for(const auto &entry:result.toList())byName.insert(entry.toMap().value("name").toString(),entry);
            return byName;
        };
        QVERIFY(!listed().contains("Control Panel"));
        QVERIFY(dispatch("control-panel"));
        QTRY_VERIFY(listed().contains("Control Panel"));
        QVERIFY(dispatch("calculator"));
        QTRY_VERIFY(listed().value("Calculator").toMap().value("active").toBool());
        QVERIFY(!listed().value("Control Panel").toMap().value("active").toBool());
        // Choosing it, as from the menu or the switcher, brings that window forward.
        QVERIFY(dispatch("app","shell:control-panel"));
        QTRY_VERIFY(listed().value("Control Panel").toMap().value("active").toBool());
        QVERIFY(!listed().value("Calculator").toMap().value("active").toBool());
        // In the Application menu too, by the same list.
        QVariant entries;
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(),"shellMenuEntries",Q_RETURN_ARG(QVariant,entries),
            Q_ARG(QVariant,view.rootObject()->property("barEntries").toList().size())));
        QStringList names;
        for(const auto &entry:entries.toList())names<<entry.toMap().value("text").toString();
        QVERIFY2(names.contains("Control Panel") && names.contains("Calculator"),qPrintable(names.join(", ")));
    }
    void printableCommandEventsKeepNoteAndPanelCommandsWorking()
    {
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        auto *notes=qobject_cast<StickyNotes *>(manager.stickies());
        QVERIFY(notes);
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto dispatch=[&](const char *action){return QMetaObject::invokeMethod(view.rootObject(),"dispatchShellCommand",
            Q_ARG(QVariant,QVariant(action)),Q_ARG(QVariant,QVariant(0)));};
        const auto chord=[&](Qt::Key key,uint code,const QString &text){
            nativeKeyEvent(&view,QEvent::KeyPress,Qt::Key_Meta,Qt::MetaModifier,125);
            nativeKeyEvent(&view,QEvent::KeyPress,key,Qt::MetaModifier,code,text);
            nativeKeyEvent(&view,QEvent::KeyRelease,key,Qt::MetaModifier,code);
            nativeKeyEvent(&view,QEvent::KeyRelease,Qt::Key_Meta,Qt::NoModifier,125);
        };
        QVERIFY(dispatch("sticky-new"));
        QTRY_COMPARE(notes->count(),1);
        const int first=notes->data(notes->index(0),StickyNotes::IdRole).toInt();
        auto *edit=visualItem(view.contentItem(),"sticky-text-"+QString::number(first));
        QVERIFY(edit);
        QTRY_VERIFY(edit->hasActiveFocus());
        for(const char c:QByteArray("keep this note"))QTest::keyClick(&view,c);
        chord(Qt::Key_A,30,"a");
        QTRY_COMPARE(edit->property("selectedText").toString(),QString("keep this note"));
        chord(Qt::Key_C,46,"c");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(),QString("keep this note"));
        chord(Qt::Key_N,49,"n");
        QTRY_COMPARE(notes->count(),2);
        QCOMPARE(edit->property("text").toString(),QString("keep this note"));
        // W belongs to the new empty note. It must reach QML instead of being
        // swallowed by the window filter, and must not type into either note.
        QTest::qWait(50);
        chord(Qt::Key_W,17,"w");
        QTRY_COMPARE(notes->count(),1);
        QCOMPARE(edit->property("text").toString(),QString("keep this note"));
        QVERIFY(dispatch("keyboard"));
        auto *panels=visualItem(view.contentItem(),"control-panels-window");
        auto *field=visualItem(view.contentItem(),"keyboard-try");
        QVERIFY(panels && field);
        QTRY_VERIFY(panels->isVisible());
        field->forceActiveFocus();
        QTRY_VERIFY(field->hasActiveFocus());
        chord(Qt::Key_W,17,"w");
        QTRY_VERIFY(!panels->isVisible());
        QVERIFY(field->property("text").toString().isEmpty());
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
    void quickControlsUseAnAsynchronousFakeAndCloseWithEscape()
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
        QVERIFY(!manager.shellModal()); // A panel of the modeless Control Panels window.
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
        QTRY_VERIFY(!panel->property("opened").toBool());
        // Sound Volume is a slider, as in 9.2.2: held down, it shows the level (75 here); dragging
        // the pointer up and releasing sets the new level; a click that doesn't move sets nothing.
        {
            auto *module=visualItem(view.contentItem(),"control-strip-sound");
            QVERIFY(module);
            const QPoint at=module->mapToScene(QPointF(module->width()/2,module->height()/2)).toPoint();
            auto *slider=visualItem(view.contentItem(),"control-strip-volume-slider");
            auto *thumb=visualItem(view.contentItem(),"control-strip-volume-thumb");
            QVERIFY(slider && thumb && !slider->isVisible());
            QTest::mousePress(&view,Qt::LeftButton,{},at);
            QTRY_VERIFY(slider->isVisible());
            QTRY_COMPARE(lastCall(),QByteArray("status"));
            // 21 px wide, its top 63 px above the strip, 5 px in from the module's left.
            auto *strip=visualItem(view.contentItem(),"control-strip");
            QCOMPARE(qRound(slider->y()),qRound(strip->y())-63);
            QCOMPARE(qRound(slider->x()),qRound(module->mapToScene(QPointF(0,0)).x())+5);
            QCOMPARE(qRound(thumb->y()),3+qRound(46*25/100.0));
            QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/control-strip-sound.png"));
            QTest::mouseRelease(&view,Qt::LeftButton,{},at);
            QTRY_VERIFY(!slider->isVisible());
            QTest::qWait(200);
            QCOMPARE(lastCall(),QByteArray("status"));
            QTest::mousePress(&view,Qt::LeftButton,{},at);
            QTRY_VERIFY(slider->isVisible());
            QTest::mouseMove(&view,at-QPoint(0,40),20);
            QTest::mouseMove(&view,at-QPoint(0,200),20);
            QTRY_COMPARE(qRound(thumb->y()),3);
            QTest::mouseRelease(&view,Qt::LeftButton,{},at-QPoint(0,200),20);
            QTRY_COMPARE_WITH_TIMEOUT(lastCall(),QByteArray("set volume 100"),8000);
            QTRY_VERIFY_WITH_TIMEOUT(!controls.busy(),8000);
            QVERIFY(!slider->isVisible());
            QVERIFY(!visualItem(view.contentItem(),"control-strip-menu")->isVisible());
        }
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
    void scrapbookKeepsTextAndPicturesInOrderAndShowsThem()
    {
        QTemporaryDir root;
        const auto profile = root.path() + "/profile";
        QImage square(40, 30, QImage::Format_ARGB32);
        square.fill(Qt::red);
        QByteArray png;
        {
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            square.save(&buffer, "PNG");
        }
        const QUrl dataUrl("data:image/png;base64," + QString::fromLatin1(png.toBase64()));
        {
            Scrapbook book(profile);
            QCOMPARE(book.count(), 0);
            QVERIFY(book.addText("second", 0));
            QVERIFY(book.addPicture(dataUrl, 0)); // In front of the item shown.
            QVERIFY(book.addText("third", 2));
            QVERIFY(!book.addText("", 0));
            QVERIFY(!book.addPicture(QUrl("data:image/png;base64,bm90IGEgcGljdHVyZQ=="), 0));
            QCOMPARE(book.count(), 3);
            QCOMPARE(book.item(0).value("kind").toString(), QString("picture"));
            QCOMPARE(book.item(0).value("width").toInt(), 40);
            QCOMPARE(book.item(1).value("text").toString(), QString("second"));
            QVERIFY(book.item(9).isEmpty());
            QVERIFY(book.addText("fourth", 3));
        }
        // A link planted in place of an item is never followed: that item is dropped.
        {
            QFile index(profile + "/scrapbook/index.json");
            QVERIFY(index.open(QIODevice::ReadOnly));
            const auto items = QJsonDocument::fromJson(index.readAll()).object().value("items").toArray();
            QCOMPARE(items.size(), 4);
            const auto planted = profile + "/scrapbook/" + items[3].toObject().value("file").toString();
            QFile secret(root.path() + "/secret.txt");
            QVERIFY(secret.open(QIODevice::WriteOnly) && secret.write("private") > 0);
            secret.close();
            QVERIFY(QFile::remove(planted));
            QVERIFY(QFile::link(root.path() + "/secret.txt", planted));
        }
        Scrapbook reopened(profile);
        QCOMPARE(reopened.count(), 3);
        QCOMPARE(reopened.item(2).value("text").toString(), QString("third"));
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), profile, {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *book = qobject_cast<Scrapbook *>(manager.scrapbook());
        QVERIFY(book);
        QCOMPARE(book->count(), 3);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant("scrapbook")), Q_ARG(QVariant, QVariant(0))));
        auto *window = visualItem(view.contentItem(), "scrapbook-window");
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible() && window->hasActiveFocus());
        const auto text = [&](const QString &name) { return visualItem(view.contentItem(), name)->property("text").toString(); };
        QCOMPARE(text("scrapbook-position"), QString("1 of 3"));
        QCOMPARE(text("scrapbook-type"), QString("picture"));
        QVERIFY(text("scrapbook-size").contains("40 × 30"));
        QVERIFY(visualItem(view.contentItem(), "scrapbook-picture")->isVisible());
        QTest::keyClick(&view, Qt::Key_Right);
        QTRY_COMPARE(text("scrapbook-position"), QString("2 of 3"));
        QCOMPARE(text("scrapbook-text"), QString("second"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/scrapbook.png"));
        QTest::keyClick(&view, Qt::Key_Left);
        QTest::keyClick(&view, Qt::Key_Left);
        QTRY_COMPARE(text("scrapbook-position"), QString("3 of 3"));
        // Clear (Delete) removes the item shown; the next one takes its place.
        QTest::keyClick(&view, Qt::Key_Delete);
        QTRY_COMPARE(book->count(), 2);
        QCOMPARE(text("scrapbook-position"), QString("2 of 2"));
        QCOMPARE(text("scrapbook-text"), QString("second"));
        // The desktop's Edit menu acts on the Scrapbook while it is in front: Edit ▸ Clear.
        auto *edit = visualItem(view.contentItem(), "bar-entry-2");
        QVERIFY(edit);
        QTest::mouseClick(&view, Qt::LeftButton, {}, edit->mapToScene(QPointF(edit->width() / 2, 12)).toPoint());
        QQuickItem *clearRow = nullptr;
        QTRY_VERIFY((clearRow = visualItem(view.contentItem(), "desktop-command-edit-clear")) && clearRow->isVisible());
        clearRow->parentItem()->ensurePolished();
        QTest::mouseClick(&view, Qt::LeftButton, {}, clearRow->mapToScene(QPointF(clearRow->width() / 2, clearRow->height() / 2)).toPoint());
        QTRY_COMPARE(book->count(), 1);
        QTRY_VERIFY(window->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_W, Qt::MetaModifier);
        QTRY_VERIFY(!window->isVisible());
        Scrapbook saved(profile);
        QCOMPARE(saved.count(), 1);
    }
    void assetImporterFindsAMacDiskImportsAndReportsEachKind()
    {
        QTemporaryDir root;
        // Personal assets go under XDG_DATA_HOME: keep them in this test's folder.
        const auto previousData = qgetenv("XDG_DATA_HOME");
        const auto restoreData = qScopeGuard([&] { qputenv("XDG_DATA_HOME", previousData); });
        qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
        // A stand-in Mac disk: a System Folder whose System file carries an empty resource fork,
        // copied the AppleDouble way ("._System"). No Apple data.
        const auto disk = root.path() + "/disks/Test HD";
        QVERIFY(QDir().mkpath(disk + "/System Folder"));
        QByteArray fork(256, '\0');
        QDataStream(&fork, QIODevice::WriteOnly) << quint32(256) << quint32(256) << quint32(0) << quint32(30);
        QByteArray map(30, '\0');
        map[25] = 28; map[27] = 30; map[28] = char(0xFF); map[29] = char(0xFF);
        fork += map;
        QByteArray dbl;
        {
            QDataStream out(&dbl, QIODevice::WriteOnly);
            out << quint32(0x00051607) << quint32(0x00020000);
            out.writeRawData(QByteArray(16, '\0').constData(), 16);
            out << quint16(1) << quint32(2) << quint32(38) << quint32(fork.size());
        }
        dbl += fork;
        QFile system(disk + "/System Folder/System"), apple(disk + "/System Folder/._System");
        QVERIFY(system.open(QIODevice::WriteOnly) && apple.open(QIODevice::WriteOnly));
        apple.write(dbl); apple.close(); system.close();
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        QVERIFY(manager.personalAssets().startsWith(root.path()));
        auto *service = manager.assetImporter();
        QVERIFY(service->setProperty("searchRoots", QStringList{root.path() + "/disks"}));
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant("asset-importer")), Q_ARG(QVariant, QVariant(0))));
        auto *window = visualItem(view.contentItem(), "asset-importer-window");
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        // It finds the disk by its System Folder; choosing it fills in the path.
        QTRY_VERIFY_WITH_TIMEOUT(visualItem(view.contentItem(), "asset-importer-source-0"), 20000);
        QCOMPARE(service->property("sources").toList().size(), 1);
        QCOMPARE(service->property("sources").toList().first().toMap().value("path").toString(), disk);
        auto *row = visualItem(view.contentItem(), "asset-importer-source-0");
        QTest::mouseClick(&view, Qt::LeftButton, {}, row->mapToScene(QPointF(40, 10)).toPoint());
        QTRY_COMPARE(visualItem(view.contentItem(), "asset-importer-path")->property("text").toString(), disk);
        auto *import = visualItem(view.contentItem(), "asset-importer-import");
        QTRY_VERIFY(import->isEnabled());
        QTest::mouseClick(&view, Qt::LeftButton, {}, import->mapToScene(QPointF(10, 10)).toPoint());
        // Every kind is reported; this disk has none of them, and nothing went wrong.
        QTRY_VERIFY_WITH_TIMEOUT(visualItem(view.contentItem(), "asset-importer-section-6"), 30000);
        QCOMPARE(service->property("error").toString(), QString());
        const auto sections = service->property("sections").toList();
        QCOMPARE(sections.size(), 7);
        QCOMPARE(sections.first().toMap().value("name").toString(), QString("Alert sounds"));
        QVERIFY(visualItem(view.contentItem(), "asset-importer-section-0")->property("text").toString()
                    .endsWith("not on this disk"));
        QVERIFY(QDir(manager.personalAssets()).exists());
        // A folder without a System Folder is refused with the importer's own reason.
        QVERIFY(QMetaObject::invokeMethod(service, "importFrom", Q_ARG(QString, root.path() + "/disks"), Q_ARG(bool, false)));
        QTRY_VERIFY_WITH_TIMEOUT(!service->property("busy").toBool(), 30000);
        QVERIFY2(service->property("error").toString().startsWith("No Mac OS System Folder"),
                 qPrintable(service->property("error").toString()));
        QTRY_VERIFY(visualItem(view.contentItem(), "asset-importer-error")->isVisible());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/asset-importer.png"));
    }
    void softwareUpdateChecksALocalPackageAndRefusesAChangedOne()
    {
        QTemporaryDir root;
        // A throwaway signing key, never the real release key.
        const auto gnupg = root.path() + "/gnupg";
        QVERIFY(QDir().mkpath(gnupg));
        QFile::setPermissions(gnupg, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const auto gpg = [&](const QStringList &arguments) {
            QProcess process;
            process.start("gpg", QStringList{"--homedir", gnupg, "--batch", "--pinentry-mode", "loopback",
                                             "--passphrase", ""} + arguments);
            process.waitForFinished(60000);
            return process.exitCode() == 0 ? process.readAllStandardOutput() : QByteArray();
        };
        QVERIFY(gpg({"--quick-gen-key", "Test <test@example.invalid>", "ed25519", "sign", "never"}).isEmpty());
        const auto keyring = root.path() + "/keys.gpg";
        {
            QFile keys(keyring);
            QVERIFY(keys.open(QIODevice::WriteOnly));
            QVERIFY(keys.write(gpg({"--export"})) > 0);
        }
        // A release folder as on the external SSD: newer than anything installed.
        const auto bundle = root.path() + "/SSD/platinum-reality-99.0";
        QVERIFY(QDir().mkpath(bundle));
        const QString name = "platinum-reality-99.0-1-x86_64.pkg.tar.zst";
        const QByteArray payload("test package");
        const auto write = [&](const QString &file, const QByteArray &data) {
            QFile out(bundle + "/" + file);
            return out.open(QIODevice::WriteOnly) && out.write(data) == data.size();
        };
        QVERIFY(write(name, payload));
        QJsonObject release{{"format", 1}, {"name", "platinum-reality"}, {"version", "99.0-1"},
                            {"date", "2026-11-09T00:00:00Z"}, {"valid_until", "2099-01-01T00:00:00Z"},
                            {"arch", "x86_64"}, {"package", name},
                            {"sha256", QString(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())},
                            {"size", payload.size()}, {"notes", "Faster folders."}};
        QVERIFY(write("release.json", QJsonDocument(release).toJson()));
        gpg({"--detach-sign", "-o", bundle + "/" + name + ".sig", bundle + "/" + name});
        gpg({"--detach-sign", "-o", bundle + "/release.json.sig", bundle + "/release.json"});
        QVERIFY(QFileInfo::exists(bundle + "/release.json.sig"));

        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        SoftwareUpdate service(QCoreApplication::applicationDirPath() + "/platinum-software-update", keyring,
                               root.path() + "/software-update.json");
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *window = view.rootObject()->findChild<QObject *>("software-update-window");
        QVERIFY(window && window->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&service))));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant("software-update")), Q_ARG(QVariant, QVariant(0))));
        QTRY_VERIFY(visualItem(view.contentItem(), "software-update-window")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        const auto click = [&](const QString &objectName) {
            auto *item = visualItem(view.contentItem(), objectName);
            if (!item || !item->isVisible() || !item->isEnabled()) return false;
            QTest::mouseClick(&view, Qt::LeftButton, {}, item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        // Install from Local Package: the folder is typed, checked, and offered with its notes.
        QVERIFY(click("software-update-local"));
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        auto *path = visualItem(view.contentItem(), "software-update-path");
        QTRY_VERIFY(path && path->isVisible());
        QVERIFY(path->setProperty("text", bundle));
        QVERIFY(click("software-update-use"));
        QTRY_VERIFY_WITH_TIMEOUT(!service.ready().isEmpty() || !service.error().isEmpty(), 20000);
        QVERIFY2(service.error().isEmpty(), qPrintable(service.error()));
        QTRY_VERIFY(visualItem(view.contentItem(), "software-update-install")->isVisible());
        QCOMPARE(visualItem(view.contentItem(), "software-update-result")->property("text").toString(),
                 QString("Platinum Reality 99.0-1 is ready to install."));
        QCOMPARE(visualItem(view.contentItem(), "software-update-notes")->property("text").toString(),
                 QString("Faster folders."));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/software-update.png"));
        // The same folder with a changed package is refused, and Install goes away. The first
        // check's helper may still be exiting; a check asked for while busy is ignored.
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY(write(name, "test packagf"));
        service.useLocal(bundle);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY2(service.error().contains("checksum"), qPrintable(service.error()));
        QVERIFY(service.ready().isEmpty());
        QTRY_VERIFY(!visualItem(view.contentItem(), "software-update-install")->isVisible());
        // Automatic checks are off until chosen, and the choice is kept.
        QVERIFY(!service.automatic());
        QVERIFY(click("software-update-automatic"));
        QTRY_VERIFY(service.automatic());
        SoftwareUpdate again(QCoreApplication::applicationDirPath() + "/platinum-software-update", keyring,
                             root.path() + "/software-update.json");
        QVERIFY(again.automatic());
    }
    // After an install, Software Update offers Restart Desktop instead of asking for Log Out
    // (Lily Hewitt, 2026-11-12). A stand-in updater reports a finished install.
    void softwareUpdateOffersRestartDesktopAfterInstalling()
    {
        QTemporaryDir root;
        qputenv("PLATINUM_DESKTOP_RESTART", "75");
        const auto unset = qScopeGuard([] { qunsetenv("PLATINUM_DESKTOP_RESTART"); });
        const auto client = root.path() + "/fake-update.py";
        QFile script(client);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("import json, sys\n"
                     "say = lambda **m: print(json.dumps(m), flush=True)\n"
                     "command = sys.argv[1]\n"
                     "if command == 'verify':\n"
                     "    say(ready=sys.argv[2], release={'version': '99.0-1', 'notes': 'Faster folders.'})\n"
                     "elif command == 'install':\n"
                     "    say(step='verify'); say(step='install', version='99.0-1'); say(done=True, version='99.0-1')\n"
                     "else:\n"
                     "    say(installed='98.0-1')\n");
        script.close();
        QVERIFY(QDir().mkpath(root.path() + "/bundle"));
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        SoftwareUpdate service(client, root.path() + "/keys.gpg", root.path() + "/software-update.json");
        QSignalSpy finished(&service, &SoftwareUpdate::installFinished);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *window = view.rootObject()->findChild<QObject *>("software-update-window");
        QVERIFY(window && window->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&service))));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant("software-update")), Q_ARG(QVariant, QVariant(0))));
        QTRY_VERIFY(visualItem(view.contentItem(), "software-update-window")->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY(!service.restartNeeded());
        QVERIFY(!visualItem(view.contentItem(), "software-update-restart-desktop")->isVisible());
        service.useLocal(root.path() + "/bundle");
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy() && !service.ready().isEmpty(), 20000);
        service.install();
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 20000);
        QVERIFY(service.restartNeeded());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QString("99.0-1"));
        QCOMPARE(service.message(), QString("Platinum Reality 99.0-1 is installed. Restart Desktop to use it: "
                                            "your applications close and open again."));
        auto *restart = visualItem(view.contentItem(), "software-update-restart-desktop");
        QTRY_VERIFY(restart->isVisible() && restart->isEnabled());
        // Restart Desktop ends this desktop for a new one, noting Software Update to open again.
        QSignalSpy ended(&manager, &NativeManager::sessionEnded);
        QTest::mouseClick(&view, Qt::LeftButton, {}, restart->mapToScene(QPointF(restart->width() / 2, restart->height() / 2)).toPoint());
        QTRY_COMPARE(ended.count(), 1);
        QCOMPARE(ended.at(0).at(0).toInt(), NativeManager::RestartDesktopExit);
        QFile note(root.path() + "/profile/reopen-after-restart.json");
        QVERIFY(note.open(QIODevice::ReadOnly));
        QVERIFY(QJsonDocument::fromJson(note.readAll()).object().value("shell").toArray().contains("software-update"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/software-update-restart.png"));
    }
    void softwareUpdateChecksAfterInstallingUntilTheNetworkAnswers()
    {
        // A stand-in for platinum-software-update: offline first, then GitHub has 99.0-1.
        QTemporaryDir root;
        const auto client = root.path() + "/client.py", online = root.path() + "/online";
        {
            QFile script(client);
            QVERIFY(script.open(QIODevice::WriteOnly));
            script.write("import json, os, sys\n"
                         "if sys.argv[1] == 'status': print(json.dumps({'installed': '0.46-1'})); sys.exit(0)\n"
                         "if not os.path.exists(" + QByteArray("'") + online.toUtf8() + "'):\n"
                         "    print(json.dumps({'error': 'GitHub could not be reached (offline).'})); sys.exit(1)\n"
                         "print(json.dumps({'installed': '0.46-1', 'available': {'version': '99.0-1', 'tag': 'v99.0',"
                         " 'date': '2026-11-12T00:00:00Z', 'notes': 'Newer.', 'size': 1}}))\n");
        }
        const auto settings = root.path() + "/software-update.json";
        const auto pending = [&] {
            QFile file(settings);
            return file.open(QIODevice::ReadOnly) && QJsonDocument::fromJson(file.readAll()).object().value("checkPending").toBool();
        };
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto service = std::make_unique<SoftwareUpdate>(client, root.path() + "/keys.gpg", settings);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *window = view.rootObject()->findChild<QObject *>("software-update-window");
        QVERIFY(window && window->setProperty("service", QVariant::fromValue(static_cast<QObject *>(service.get()))));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        // Offline: the check fails quietly, stays pending, and nothing opens.
        service->checkAfterInstall();
        QTRY_VERIFY_WITH_TIMEOUT(!service->busy(), 20000);
        QVERIFY(service->error().isEmpty());
        QVERIFY(pending());
        QVERIFY(!visualItem(view.contentItem(), "software-update-window")->isVisible());
        // Still pending after logging out and in again.
        service = std::make_unique<SoftwareUpdate>(client, root.path() + "/keys.gpg", settings);
        QVERIFY(window->setProperty("service", QVariant::fromValue(static_cast<QObject *>(service.get()))));
        QVERIFY(pending());
        // Online: the check answers, the window shows the newer release, and the check is done.
        QFile(online).open(QIODevice::WriteOnly);
        QSignalSpy found(service.get(), &SoftwareUpdate::updateFound);
        service->checkAfterInstall();
        QTRY_COMPARE_WITH_TIMEOUT(found.count(), 1, 20000);
        QCOMPARE(found.first().first().toString(), QString("99.0-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!service->busy(), 20000);
        QVERIFY(!pending());
        QTRY_VERIFY(visualItem(view.contentItem(), "software-update-window")->isVisible());
        QTRY_VERIFY(visualItem(view.contentItem(), "software-update-download")->isVisible());
    }
    void keyCapsShowWhatTheLayoutTypesWithShiftAndOption()
    {
        KeyCaps caps;
        QCOMPARE(caps.layout(), QString("us"));
        QCOMPARE(caps.labels(false, false).value("30").toString(), QString("a"));
        QCOMPARE(caps.labels(true, false).value("30").toString(), QString("A"));
        QCOMPARE(caps.labels(true, false).value("2").toString(), QString("!"));
        caps.setLayout("de");
        QCOMPARE(caps.layout(), QString("de"));
        QCOMPARE(caps.labels(false, false).value("21").toString(), QString("z")); // QWERTZ
        QCOMPARE(caps.labels(false, true).value("16").toString(), QString("@"));  // Option-Q
        // Dead keys are reported apart, with the accent they put on the next letter.
        caps.setLayout("us(intl)");
        QCOMPARE(caps.layout(), QString("us(intl)"));
        QCOMPARE(caps.deadKeys(false, false).value("40").toString(), QString("´"));
        QVERIFY(!caps.labels(false, false).contains("40"));
        caps.setLayout("../etc");
        QCOMPARE(caps.layout(), QString("us(intl)"));
        caps.setLayout("us");
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant("key-caps")), Q_ARG(QVariant, QVariant(0))));
        auto *window = visualItem(view.contentItem(), "key-caps-window");
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible() && window->hasActiveFocus());
        auto *a = visualItem(view.contentItem(), "key-caps-key-30");
        QVERIFY(a);
        QCOMPARE(a->property("label").toString(), QString("a"));
        QTest::keyPress(&view, Qt::Key_Shift, Qt::ShiftModifier);
        QTRY_COMPARE(a->property("label").toString(), QString("A"));
        QVERIFY(visualItem(view.contentItem(), "key-caps-key-42")->property("down").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/key-caps.png"));
        QTest::keyRelease(&view, Qt::Key_Shift, Qt::NoModifier);
        QTRY_COMPARE(a->property("label").toString(), QString("a"));
        for (const char c : QByteArray("hi")) QTest::keyClick(&view, c);
        QTRY_COMPARE(visualItem(view.contentItem(), "key-caps-typed")->property("text").toString(), QString("hi"));
        // Clicking a key types it too.
        QTest::mouseClick(&view, Qt::LeftButton, {}, a->mapToScene(QPointF(a->width() / 2, a->height() / 2)).toPoint());
        QTRY_COMPARE(visualItem(view.contentItem(), "key-caps-typed")->property("text").toString(), QString("hia"));
        QTest::keyClick(&view, Qt::Key_W, Qt::MetaModifier);
        QTRY_VERIFY(!window->isVisible());
    }
    void notePadKeepsEightPagesAndTurnsAtItsCorner()
    {
        QTemporaryDir root;
        {
            NotePad pad(root.path() + "/profile");
            QCOMPARE(pad.pageCount(), 8);
            QCOMPARE(pad.page(), 0);
            pad.setText(0, "first");
            pad.setText(7, QString(40000, 'x')); // Bounded to 32,768 characters.
            pad.setText(8, "nowhere");
            pad.setPage(3);
            pad.setPage(9);
            QCOMPARE(pad.page(), 3);
            QVERIFY(pad.saveNow());
        }
        {
            NotePad pad(root.path() + "/profile");
            QCOMPARE(pad.text(0), QString("first"));
            QCOMPARE(pad.text(7).size(), 32768);
            QCOMPARE(pad.page(), 3);
            pad.setPage(0);
            QVERIFY(pad.saveNow());
        }
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *pad = qobject_cast<NotePad *>(manager.notePad());
        QVERIFY(pad);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant("note-pad")), Q_ARG(QVariant, QVariant(0))));
        auto *window = visualItem(view.contentItem(), "note-pad-window");
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        auto *editor = visualItem(view.contentItem(), "note-pad-text");
        QVERIFY(editor);
        QTRY_VERIFY(editor->hasActiveFocus());
        QCOMPARE(editor->property("text").toString(), QString("first"));
        auto *number = visualItem(view.contentItem(), "note-pad-page-number");
        QCOMPARE(number->property("text").toString(), QString("1"));
        for (const char c : QByteArray(" page")) QTest::keyClick(&view, c);
        QTRY_COMPARE(pad->text(0), QString("first page"));
        auto *corner = visualItem(view.contentItem(), "note-pad-corner");
        QVERIFY(corner);
        const auto at = [&](qreal x, qreal y) { return corner->mapToScene(QPointF(x, y)).toPoint(); };
        // The flap, above the fold: the next page. The corner below it: back.
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(corner->width() - 4, 4));
        QTRY_COMPARE(pad->page(), 1);
        QCOMPARE(number->property("text").toString(), QString("2"));
        QCOMPARE(editor->property("text").toString(), QString());
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(3, corner->height() - 3));
        QTRY_COMPARE(pad->page(), 0);
        QCOMPARE(editor->property("text").toString(), QString("first page"));
        // Back from the first page is the last.
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(3, corner->height() - 3));
        QTRY_COMPARE(pad->page(), 7);
        QCOMPARE(number->property("text").toString(), QString("8"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/note-pad.png"));
        // Command-W puts it away; the pages are saved.
        QTest::keyClick(&view, Qt::Key_W, Qt::MetaModifier);
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(pad->saveNow());
        NotePad saved(root.path() + "/profile");
        QCOMPARE(saved.text(0), QString("first page"));
        QCOMPARE(saved.page(), 7);
    }
    void generalControlsPersistBlinkMenusAndSetTheInsertionPoint()
    {
        // General Controls (9.2.2): show the desktop in the background, menu blinking Off-3 and the
        // insertion point's speed. Defaults are 9.2.2's: shown, three blinks, medium.
        QTemporaryDir root;
        {
            AppearanceSettings settings(root.path() + "/profile");
            QCOMPARE(settings.general().value("showDesktop").toBool(), true);
            QCOMPARE(settings.general().value("menuBlinks").toInt(), 3);
            QCOMPARE(settings.general().value("caretBlink").toInt(), 1);
            QSignalSpy errors(&settings, &AppearanceSettings::errorOccurred);
            QVERIFY(!settings.setGeneralOption("menuBlinks", 4));
            QVERIFY(!settings.setGeneralOption("caretBlink", -1));
            QVERIFY(!settings.setGeneralOption("showDesktop", 1));
            QVERIFY(!settings.setGeneralOption("documents", true));
            QCOMPARE(errors.count(), 4);
            QVERIFY(settings.setGeneralOption("showDesktop", false));
            QVERIFY(settings.setGeneralOption("caretBlink", 2));
            QCOMPARE(QGuiApplication::styleHints()->cursorFlashTime(), 500);
            AppearanceSettings restored(root.path() + "/profile");
            QCOMPARE(restored.general().value("showDesktop").toBool(), false);
            QCOMPARE(restored.general().value("caretBlink").toInt(), 2);
            QVERIFY(settings.setGeneralOption("showDesktop", true));
            QVERIFY(settings.setGeneralOption("caretBlink", 1));
            QCOMPARE(QGuiApplication::styleHints()->cursorFlashTime(), 1000);
        }
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status() != QQuickView::Error,
                 qPrintable(view.errors().isEmpty() ? QString() : view.errors().first().toString()));
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto click = [&](const QString &name)
        {
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                view.grabWindow();
                auto *item = visualItem(view.contentItem(), name);
                if (item && item->isVisible() && item->width() > 0)
                {
                    QTest::mouseClick(&view, Qt::LeftButton, {},
                                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                    return true;
                }
                QTest::qWait(20);
            }
            return false;
        };
        auto *appearance = view.rootObject()->findChild<QObject *>("appearance-dialog");
        QVERIFY(appearance);
        // Three blinks: the command waits for them, the item going dark and lit again.
        QVERIFY(click("control-strip-appearance"));
        QVERIFY(click("desktop-command-appearance"));
        QVERIFY(!appearance->property("opened").toBool());
        auto *row = visualItem(view.contentItem(), "desktop-command-appearance");
        QVERIFY(row);
        QVERIFY(!row->property("lit").toBool());
        QTRY_VERIFY(row->property("lit").toBool());
        QTRY_VERIFY(appearance->property("opened").toBool());
        // Off: the command runs at once.
        QVERIFY(settings->setGeneralOption("menuBlinks", 0));
        auto *panels = view.rootObject()->findChild<QObject *>("control-panels-window");
        QVERIFY(panels);
        QMetaObject::invokeMethod(panels, "close");
        QTRY_VERIFY(!appearance->property("opened").toBool());
        QVERIFY(click("control-strip-appearance"));
        QVERIFY(click("desktop-command-appearance"));
        QVERIFY(appearance->property("opened").toBool());
        // The panel itself, in the list between Energy Saver and Keyboard.
        QVERIFY(click("control-panel-general"));
        auto *general = view.rootObject()->findChild<QObject *>("general-controls-panel");
        QVERIFY(general);
        QTRY_VERIFY(general->property("opened").toBool());
        QVERIFY(click("general-show-desktop"));
        QTRY_VERIFY(!settings->general().value("showDesktop").toBool());
        // The Finder is in front here, so the desktop's icons stay.
        auto *trash = visualItem(view.contentItem(), "desktop-entry-Trash");
        if (trash)
            QVERIFY(trash->isVisible());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/general-controls.png"));
        QVERIFY(click("general-show-desktop"));
        QTRY_VERIFY(settings->general().value("showDesktop").toBool());
    }
    void theAppleMenuShowsTheImportedRainbowApple()
    {
        // Asset Importer writes <data>/platinum-reality/assets/icons/apple-menu-16.png from the user's
        // Mac OS 9 System file; nothing put it where the menu bar looked (found 2026-11-11).
        QTemporaryDir data, profile;
        const auto previous = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", data.path().toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("XDG_DATA_HOME", previous); });
        const auto picture = [](const QString &path, QColor color)
        {
            QDir().mkpath(QFileInfo(path).path());
            QImage image(16, 16, QImage::Format_ARGB32);
            image.fill(color);
            return image.save(path);
        };
        QVERIFY(!AppearanceSettings(profile.path()).menuEmblem().toLocalFile().startsWith(data.path()));
        const auto imported = data.filePath("platinum-reality/assets/icons/apple-menu-16.png");
        QVERIFY(picture(imported, Qt::red));
        QCOMPARE(AppearanceSettings(profile.path()).menuEmblem(), QUrl::fromLocalFile(imported));
        // A picture the user put in the profile still comes first.
        const auto own = profile.filePath("assets/apple-menu.png");
        QVERIFY(picture(own, Qt::blue));
        QCOMPARE(AppearanceSettings(profile.path()).menuEmblem(), QUrl::fromLocalFile(own));
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
        QCOMPARE(restored.controlStripModules(), 0);
        // Choosing a length (the tab drag) also expands the strip.
        QVERIFY(settings.setControlStripModules(3));
        AppearanceSettings shortened(root.path());
        QCOMPARE(shortened.controlStripModules(), 3);
        QVERIFY(!shortened.controlStripCollapsed());
        QSignalSpy errors(&settings, &AppearanceSettings::errorOccurred);
        QVERIFY(!settings.apply(-1, true));
        QVERIFY(!settings.apply(1201, true));
        QVERIFY(!settings.setControlStripPosition(-1, 30));
        QVERIFY(!settings.setControlStripModules(-1));
        QCOMPARE(errors.count(), 4);
        AppearanceSettings unchanged(root.path());
        QCOMPARE(unchanged.shadeDuration(), 600);
        QVERIFY(!unchanged.controlStripVisible());
        QCOMPARE(unchanged.controlStripModules(), 3);
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
        // The same selection as a PNG picture on the clipboard.
        QVERIFY(tool.copy());
        const auto *copied = QGuiApplication::clipboard()->mimeData();
        QVERIFY(copied && copied->hasFormat("image/png"));
        const auto picture = QImage::fromData(copied->data("image/png"), "PNG");
        QCOMPARE(picture.size(), QSize(100,100));
        QCOMPARE(picture.pixelColor(30,30), QColor(Qt::blue));
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

    void desktopPicturePanelPreviewsSetsAndRestores()
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
        QVERIFY(!manager.shellModal()); // A panel of the modeless Control Panels window.
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
        // Closing without Set Desktop leaves the desktop as it was; reopening starts from it.
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        QVERIFY(settings->wallpaperPath().isEmpty());
        QVERIFY(open());
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(panel->property("draftPath").toString(), QString());
        panel->setProperty("draftPath", picture);
        QVERIFY(click("picture-layout-fit"));
        QVERIFY(click("picture-set"));
        QTRY_COMPARE(settings->wallpaperPath(), picture);
        QVERIFY(!visualItem(view.contentItem(), "picture-set")->isEnabled()); // Nothing left to set.
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
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
        QVERIFY(click("picture-set"));
        QTRY_VERIFY(settings->wallpaper().isEmpty());
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
    void monitorsPanelSavesUseAllDisplaysForTheNextLoginAtOnce()
    {
        // Lily Hewitt (2026-10-21): two monitors, opt-in. The choice is saved in the desktop profile for
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
        // A panel of the Control Panels window: the box saves as soon as it is clicked.
        QTRY_COMPARE(saved(), QString("extend"));
        QVERIFY(manager.useAllDisplays());
        // A later session reads the choice back.
        QQuickView other;
        NativeManager reloaded(nullptr, &other, root.path(), profile, {});
        QVERIFY(reloaded.useAllDisplays());
        // Clearing the box saves one display again; the note goes, as that is how this session started.
        click(item("monitors-use-all"));
        QTRY_COMPARE(saved(), QString("single"));
        QVERIFY(!manager.useAllDisplays());
        QVERIFY(!item("monitors-use-all")->property("checked").toBool());
        QVERIFY(!note->property("text").toString().contains("next time you log in"));
    }
    void dialogKeysFollowMacOS9()
    {
        // Finishing School FS-D01/FS-E01: Return and keypad Enter press the default (ringed)
        // button once; Escape and Command-period (Super, Lily Hewitt's Command key) cancel and discard
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
        // An OK/Cancel dialog with a draft, as the panels were before they moved into the
        // modeless Control Panels window.
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nimport QtQuick.Controls\n"
                          "ShellDialog {objectName: \"draft-dialog\"; sessionManager: manager; width: 400; title: \"Draft\"\n"
                          "    standardButtons: Dialog.Ok | Dialog.Cancel; property bool draftUseAll: false\n"
                          "    function edit() { open() }\n"
                          "    onAccepted: manager.setUseAllDisplays(draftUseAll)\n"
                          "    contentItem: Text {text: \"Use all displays?\"}}\n",
                          QUrl("qrc:/desktop/src/desktop/DraftDialogTest.qml"));
        auto *panel = component.beginCreate(view.rootContext());
        QVERIFY2(panel, qPrintable(component.errorString()));
        const auto destroyPanel = qScopeGuard([&] { delete panel; });
        panel->setProperty("parent", QVariant::fromValue(view.rootObject()));
        component.completeCreate();
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
        // Desktop Picture's folder field shows the folder on Return; nothing is set, and the
        // Control Panels window stays open.
        auto *pictures = view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(pictures);
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings);
        const auto wallpaper = settings->wallpaperPath();
        QMetaObject::invokeMethod(pictures, "edit");
        QTRY_VERIFY(pictures->property("opened").toBool());
        view.grabWindow();
        auto *folder = visualItem(view.contentItem(), "picture-folder");
        QVERIFY(folder);
        folder->forceActiveFocus();
        QTRY_VERIFY(folder->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_Return);
        QTest::qWait(300);
        QVERIFY(pictures->property("opened").toBool());
        QCOMPARE(settings->wallpaperPath(), wallpaper);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!pictures->property("opened").toBool());
        QCOMPARE(settings->wallpaperPath(), wallpaper);
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
        QVERIFY(appearance->property("soundTrackOn").toBool());
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
        // Choosing None applies at once.
        QVERIFY(QMetaObject::invokeMethod(appearance, "setSoundTrack", Q_ARG(QVariant, false)));
        QTRY_VERIFY(!sounds->enabled());
        QVERIFY(!appearance->property("soundTrackOn").toBool());
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
        // Finishing School Phase 11 and Lily Hewitt's personal assets (docs/alert-sounds.md): the public
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
            QFile stub(assets + "/sounds/Broken.wav"); // A 14-byte text stub, like two of Lily Hewitt's files.
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
    // Sound ▸ Alert Sounds ▸ Mac sounds only (Lily Hewitt, 2026-11-12), over a stand-in helper that keeps
    // the state in a file: checking it removes the desktop sounds, unchecking downloads them again, and a
    // dismissed password box (pkexec's 126, nothing said) leaves the box as it was.
    void macSoundsOnlyRemovesAndRestoresTheDesktopSounds()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto state = root.filePath("removed");
        QFile helper(root.filePath("platinum-system-sounds"));
        QVERIFY(helper.open(QIODevice::WriteOnly));
        helper.write("import json, os, sys\n"
                     "state = " + QJsonDocument(QJsonArray{state}).toJson(QJsonDocument::Compact).mid(1).chopped(1) + "\n"
                     "if sys.argv[1] != 'status' and os.path.exists(state + '.cancel'): sys.exit(126)\n"
                     "if sys.argv[1] == 'remove': open(state, 'w').close()\n"
                     "if sys.argv[1] == 'restore' and os.path.exists(state): os.unlink(state)\n"
                     "removed = os.path.exists(state)\n"
                     "print(json.dumps({'done': sys.argv[1] != 'status', 'removed': removed,\n"
                     "                  'present': [] if removed else ['freedesktop', 'ocean'], 'packages': []}))\n");
        helper.close();
        const QByteArray previous = qgetenv("FRANKENSTEIN_SYSTEM_SOUNDS_HELPER");
        const auto restore = qScopeGuard([&] { qputenv("FRANKENSTEIN_SYSTEM_SOUNDS_HELPER", previous); });
        qputenv("FRANKENSTEIN_SYSTEM_SOUNDS_HELPER", root.filePath("platinum-system-sounds").toUtf8());
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.filePath("ws"), root.filePath("desktop-profile"), {});
        auto *sounds = qobject_cast<SystemSounds *>(manager.systemSounds());
        QVERIFY(sounds && sounds->available());
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        const auto click = [&](QQuickItem *target, qreal x = -1)
        {
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              target->mapToScene(QPointF(x < 0 ? target->width() / 2 : x, target->height() / 2)).toPoint());
        };
        AudioService absent(root.filePath("absent-audio.py"));
        auto *panel = view.rootObject()->findChild<QObject *>("sound-panel");
        QVERIFY(panel);
        QVERIFY(panel->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&absent))));
        QVERIFY(QMetaObject::invokeMethod(panel, "edit"));
        QTRY_VERIFY(panel->property("opened").toBool());
        click(item("sound-tab-alerts"));
        QTRY_VERIFY(panel->property("alerts").toBool());
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = item("sound-mac-only")) && box->isVisible());
        const auto message = [&] { return item("sound-mac-only-message")->property("text").toString(); };
        QTRY_VERIFY(message().startsWith("Removes the Arch and KDE desktop sounds"));
        QVERIFY(!box->property("checked").toBool());
        // Checked: removed.
        click(box, 8);
        QTRY_VERIFY(!sounds->changing() && sounds->removed());
        QTRY_VERIFY(box->property("checked").toBool());
        QVERIFY(message().contains("updates won’t bring them back"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/sound-mac-only.png"));
        // A dismissed password box: still checked, nothing said.
        QFile cancel(state + ".cancel");
        QVERIFY(cancel.open(QIODevice::WriteOnly));
        cancel.close();
        click(box, 8);
        QTRY_VERIFY(!sounds->changing());
        QTRY_VERIFY(box->property("checked").toBool());
        QVERIFY(sounds->removed());
        QCOMPARE(sounds->error(), QString());
        // Unchecked: downloaded again.
        QVERIFY(cancel.remove());
        click(box, 8);
        QTRY_VERIFY(!sounds->changing() && !sounds->removed());
        QTRY_VERIFY(!box->property("checked").toBool());
        QCOMPARE(sounds->present(), (QStringList{"freedesktop", "ocean"}));
        // Without its helper the option isn't offered.
        SystemSounds missing(root.filePath("none"), {});
        QVERIFY(panel->setProperty("systemSounds", QVariant::fromValue(static_cast<QObject *>(&missing))));
        QTRY_VERIFY(!item("sound-mac-only")->isVisible());
    }
    void startupSoundCanBeTurnedOffAndSetLouderOrQuieter()
    {
        // Lily Hewitt, 2026-10-29: "an option to enable / disable the beep on boot up and a volume slider".
        // Sound ▸ Startup saves both for the supervisor (scripts/standalone-session.py reads
        // startup-sound.json at the next Log In); Play previews through a fake player that records.
        QTemporaryDir root;
        const auto profile = root.filePath("profile"), assets = root.filePath("assets");
        QVERIFY(QDir().mkpath(assets));
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
        const auto saved = [&]
        {
            QFile file(profile + "/startup-sound.json");
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        };
        StartupSound sound(profile, assets, player);
        // Defaults: on, at half volume (what the supervisor did before this setting).
        QVERIFY(!sound.available());
        QVERIFY(sound.enabled());
        QCOMPARE(sound.volume(), 50);
        sound.play();
        QTest::qWait(200);
        QVERIFY(recorded().isEmpty());
        // A linked or empty file is not a startup sound; WAV is preferred, as the supervisor does.
        QVERIFY(QFile::link(root.filePath("elsewhere.wav"), assets + "/startup-chime.wav"));
        {
            QFile empty(assets + "/startup-chime.flac");
            QVERIFY(empty.open(QIODevice::WriteOnly));
        }
        sound.refresh();
        QVERIFY(!sound.available());
        {
            QFile aiff(assets + "/startup-chime.aiff");
            QVERIFY(aiff.open(QIODevice::WriteOnly) && aiff.write("FORM") == 4);
        }
        QSignalSpy changed(&sound, &StartupSound::changed);
        sound.refresh();
        QVERIFY(sound.available());
        QCOMPARE(sound.fileName(), QString("startup-chime.aiff"));
        QCOMPARE(changed.size(), 1);
        sound.play();
        QTRY_COMPARE(recorded(), QStringList({"--volume 0.50 " + assets + "/startup-chime.aiff"}));
        sound.setVolume(80);
        sound.setEnabled(false);
        QCOMPARE(saved().value("volume").toInt(), 80);
        QCOMPARE(saved().value("enabled").toBool(true), false);
        {
            StartupSound reopened(profile, assets, player);
            QVERIFY(!reopened.enabled());
            QCOMPARE(reopened.volume(), 80);
        }
        sound.setEnabled(true);
        sound.setVolume(0);
        sound.play(); // Silent at volume 0.
        QTest::qWait(200);
        QCOMPARE(recorded().size(), 1);
        sound.setVolume(40);

        // The Sound panel's Startup tab.
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.filePath("desktop-profile"), {});
        QVERIFY(manager.startupSound());
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) { view.grabWindow(); return visualItem(view.contentItem(), name); };
        const auto click = [&](QQuickItem *target, qreal x = -1)
        {
            QTest::mouseClick(&view, Qt::LeftButton, {},
                              target->mapToScene(QPointF(x < 0 ? target->width() / 2 : x, target->height() / 2)).toPoint());
        };
        AudioService absent(root.filePath("absent-audio.py"));
        auto *panel = view.rootObject()->findChild<QObject *>("sound-panel");
        QVERIFY(panel);
        QVERIFY(panel->setProperty("startupSound", QVariant::fromValue(static_cast<QObject *>(&sound))));
        QVERIFY(panel->setProperty("service", QVariant::fromValue(static_cast<QObject *>(&absent))));
        QVERIFY(QMetaObject::invokeMethod(panel, "edit"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(!item("sound-startup-enabled")->isVisible());
        click(item("sound-tab-startup"));
        QTRY_VERIFY(panel->property("startup").toBool());
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = item("sound-startup-enabled")) && box->isVisible());
        QVERIFY(!item("sound-devices")->isVisible());
        QVERIFY(box->property("checked").toBool());
        QVERIFY(item("sound-startup-message")->property("text").toString().contains("startup-chime.aiff"));
        // Play previews at the chosen volume.
        click(item("sound-startup-play"));
        QTRY_COMPARE(recorded().size(), 2);
        QCOMPARE(recorded().last(), "--volume 0.40 " + assets + "/startup-chime.aiff");
        // Dragging the slider to its right end and letting go saves 100% and plays a sample.
        auto *slider = item("sound-startup-volume");
        QVERIFY(slider && slider->isEnabled());
        const auto handle = qvariant_cast<QQuickItem *>(slider->property("handle"));
        QVERIFY(handle);
        const auto start = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        const auto end = slider->mapToScene(QPointF(slider->width() + 20, slider->height() / 2)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(&view, start + (end - start) * step / 10);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, end);
        QTRY_COMPARE(sound.volume(), 100);
        QTRY_COMPARE(recorded().size(), 3);
        QCOMPARE(recorded().last(), "--volume 1.00 " + assets + "/startup-chime.aiff");
        QCOMPARE(saved().value("volume").toInt(), 100);
        // Turning it off saves that, and the volume slider is dimmed.
        click(box, 8);
        QTRY_VERIFY(!sound.enabled());
        QCOMPARE(saved().value("enabled").toBool(true), false);
        QTRY_VERIFY(!slider->isEnabled());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/sound-startup.png"));
        click(box, 8);
        QTRY_VERIFY(sound.enabled());
        // Without a sound file, the panel says where to put one, and Play is unavailable.
        QVERIFY(QFile::remove(assets + "/startup-chime.aiff"));
        QVERIFY(QMetaObject::invokeMethod(panel, "edit")); // Already open: refresh happens on reopen.
        sound.refresh();
        QTRY_VERIFY(item("sound-startup-message")->property("text").toString().contains("No startup sound is installed"));
        QVERIFY(item("sound-startup-message")->property("text").toString().contains(assets));
        QVERIFY(!item("sound-startup-play")->isEnabled());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
    }
    void eachMonitorHasItsOwnDesktopPictureNeverOneSpreadAcrossThem()
    {
        // Lily Hewitt, 2026-11-06: "i'd like my wallpaper to not be stretched across two monitors", and
        // different pictures on each monitor. Two outputs side by side, as in the Extend session.
        QTemporaryDir root;
        QVERIFY(QDir().mkpath(root.filePath("profile")));
        const auto picture = [&](const QString &name, const QColor &colour) {
            QImage image(400, 300, QImage::Format_RGB32);
            image.fill(colour);
            const auto path = root.filePath(name);
            return image.save(path) ? path : QString();
        };
        const auto red = picture("red.png", QColor("#cc2020")), blue = picture("blue.png", QColor("#2040cc"));
        QVERIFY(!red.isEmpty() && !blue.isEmpty());
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1920, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        manager.setDisplayLayout(*DisplayLayout::create({{"DP-3", QRect(0, 0, 1120, 760), 1.5, "normal"},
                                                         {"eDP-1", QRect(1120, 0, 800, 500), 2, "normal"}},
                                                        "DP-3", DisplayLayout::Mode::Extend));
        auto *appearance = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(appearance && appearance->applyMonitorPicture("", red, "fill"));
        QQuickItem *first = nullptr, *second = nullptr;
        QTRY_VERIFY((first = visualItem(view.contentItem(), "desktop-wallpaper")) &&
                    (second = visualItem(view.contentItem(), "desktop-wallpaper-eDP-1")));
        // Each monitor's own copy, the size of that monitor.
        QCOMPARE(QRectF(first->x(), first->y(), first->width(), first->height()), QRectF(0, 0, 1120, 760));
        QCOMPARE(QRectF(second->x(), second->y(), second->width(), second->height()), QRectF(1120, 0, 800, 500));
        QCOMPARE(first->property("source").toUrl().toLocalFile(), red);
        QCOMPARE(second->property("source").toUrl().toLocalFile(), red);
        QTRY_COMPARE(second->property("status").toInt(), 1);
        // A picture for the built-in display only.
        QVERIFY(appearance->applyMonitorPicture("eDP-1", blue, "fit"));
        QTRY_COMPARE(second->property("source").toUrl().toLocalFile(), blue);
        QCOMPARE(second->property("fillMode").toInt(), 1); // Image.PreserveAspectFit
        QCOMPARE(first->property("source").toUrl().toLocalFile(), red);
        QTRY_COMPARE(second->property("status").toInt(), 1);
        QTRY_COMPARE(view.grabWindow().pixelColor(560, 380), QColor("#cc2020"));
        QTRY_COMPARE(view.grabWindow().pixelColor(1520, 250), QColor("#2040cc"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/desktop-picture-per-monitor.png"));
        {
            AppearanceSettings restored(root.path() + "/profile");
            const auto own = restored.monitorPictures().value("eDP-1").toMap();
            QCOMPARE(own.value("path").toString(), blue);
            QCOMPARE(own.value("layout").toString(), QString("fit"));
            QCOMPARE(restored.wallpaperPath(), red);
        }
        // The panel offers the monitors and starts each from what it shows.
        auto *panel = view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(panel);
        QVERIFY(QMetaObject::invokeMethod(panel, "edit"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QTRY_VERIFY(visualItem(view.contentItem(), "picture-monitor-row")->isVisible());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/desktop-picture-monitor-popup.png"));
        QVERIFY(QMetaObject::invokeMethod(panel, "chooseMonitor", Q_ARG(QVariant, QVariant("eDP-1"))));
        QCOMPARE(panel->property("draftPath").toString(), blue);
        QVERIFY(QMetaObject::invokeMethod(panel, "chooseMonitor", Q_ARG(QVariant, QVariant(QString()))));
        QCOMPARE(panel->property("draftPath").toString(), red);
        QVERIFY(panel->property("changed").toBool()); // Setting it for all would replace eDP-1's own.
        // All Monitors forgets the monitors' own pictures.
        QVERIFY(appearance->applyMonitorPicture("", red, "fill"));
        QVERIFY(appearance->monitorPictures().isEmpty());
        QTRY_COMPARE(second->property("source").toUrl().toLocalFile(), red);
        // Nothing outside a monitor name or a readable picture is accepted.
        QVERIFY(!appearance->applyMonitorPicture("../DP-3", blue, "fill"));
        QVERIFY(!appearance->applyMonitorPicture("DP-3", root.filePath("missing.png"), "fill"));
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
        // Dropping the menu bar asks the supervisor at once; the desktop moves only when it publishes.
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
        QTRY_COMPARE(commands.size(), 2);
        QCOMPARE(commands.last(), QByteArray("primary DP-3\n"));
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(errors.first().first().toString(), QString("The main display could not be changed."));
        QCOMPARE(manager.primaryDisplayRect().x(), 1120);
        // The panel shows the main display as it is again.
        QTRY_COMPARE(panel->property("draftPrimary").toString(), QString("eDP-1"));
        // The refusal is shown in the desktop's modal alert; dismiss it first.
        QTRY_VERIFY(manager.shellModal());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        // Mirroring is shown, honestly unavailable.
        QMetaObject::invokeMethod(panel, "edit");
        QTRY_VERIFY(panel->property("opened").toBool());
        auto *mirror = item("monitors-mode-mirror");
        QVERIFY(mirror && !mirror->isEnabled());
        QTRY_VERIFY(visualItem(view.contentItem(), "control-panels-window")->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        QCOMPARE(commands.size(), 2);
    }
    // The Camera module over a stand-in helper that keeps the switch in a file: Camera On and Camera
    // Off with the current one checked, the cameras below, and the helper's own error shown.
    void cameraModuleSwitchesTheCamera()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto state = root.filePath("off");
        QFile helper(root.filePath("platinum-webcam"));
        QVERIFY(helper.open(QIODevice::WriteOnly));
        helper.write(("import json, os, sys\n"
                      "state = " + QJsonDocument(QJsonArray{state}).toJson(QJsonDocument::Compact).mid(1).chopped(1) + "\n"
                      "if os.path.exists(state + '.refuse') and sys.argv[1] != 'status':\n"
                      "    print(json.dumps({'error': 'Could not turn off Logitech Camera.'})); sys.exit(1)\n"
                      "if sys.argv[1] == 'off': open(state, 'w').close()\n"
                      "if sys.argv[1] == 'on' and os.path.exists(state): os.unlink(state)\n"
                      "off = os.path.exists(state)\n"
                      "print(json.dumps({'done': sys.argv[1] != 'status', 'off': off, 'others': ['IPU6 Capture'],\n"
                      "                  'cameras': [{'id': '1-6', 'name': 'Integrated Camera', 'blocked': off}]}))\n"));
        helper.close();
        CameraSwitch direct(root.filePath("platinum-webcam"), {});
        QVERIFY(direct.available());
        direct.refresh();
        QTRY_COMPARE(direct.cameras().size(), 1);
        QVERIFY(!direct.off());
        QCOMPARE(direct.others(), QStringList{"IPU6 Capture"});
        direct.setOff(true);
        QVERIFY(direct.changing());
        QTRY_VERIFY(!direct.changing() && direct.off());
        QVERIFY(direct.cameras().at(0).toMap().value("blocked").toBool());
        direct.setOff(false);
        QTRY_VERIFY(!direct.changing() && !direct.off());
        CameraSwitch missing(root.filePath("none"), {});
        QVERIFY(!missing.available());
        missing.setOff(true);
        QVERIFY(!missing.changing());
        // In the desktop: the strip's module and menu.
        const QByteArray previous = qgetenv("FRANKENSTEIN_WEBCAM_HELPER");
        const auto restore = qScopeGuard([&] { qputenv("FRANKENSTEIN_WEBCAM_HELPER", previous); });
        qputenv("FRANKENSTEIN_WEBCAM_HELPER", root.filePath("platinum-webcam").toUtf8());
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.filePath("ws"), root.filePath("desktop-profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(visualItem(view.contentItem(), "control-strip-camera"));
        auto *camera = qobject_cast<CameraSwitch *>(manager.camera());
        QVERIFY(camera);
        const auto entries = [&] {
            QVariant result;
            QMetaObject::invokeMethod(view.rootObject(), "stripMenuEntries", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QString("camera")));
            QStringList rows;
            for (const auto &entry : result.toList())
            {
                const auto map = entry.toMap();
                rows.append(map.value("separator").toBool() ? QString("-")
                            : (map.value("checked").toBool() ? "✓" : "") + map.value("text").toString()
                                  + (map.value("enabled", true).toBool() ? "" : " (dim)"));
            }
            return rows;
        };
        QTRY_VERIFY(!camera->cameras().isEmpty());
        QCOMPARE(entries(), (QStringList{"✓Camera On", "Camera Off", "-", "Integrated Camera (dim)",
                                         "IPU6 Capture (Can’t Be Turned Off) (dim)"}));
        // Turning it on while on asks nothing (no helper run).
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QString("camera-on")), Q_ARG(QVariant, QString())));
        QVERIFY(!camera->changing());
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QString("camera-off")), Q_ARG(QVariant, QString())));
        QVERIFY(camera->changing());
        QCOMPARE(entries().mid(0, 4), (QStringList{"✓Camera On (dim)", "Camera Off (dim)", "-", "Switching… (dim)"}));
        QTRY_VERIFY(!camera->changing() && camera->off());
        QCOMPARE(entries().mid(0, 2), (QStringList{"Camera On", "✓Camera Off"}));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QString("camera-on")), Q_ARG(QVariant, QString())));
        QTRY_VERIFY(!camera->changing() && !camera->off());
        // A failed switch says why, under the line.
        QFile refuse(state + ".refuse");
        QVERIFY(refuse.open(QIODevice::WriteOnly));
        refuse.close();
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QString("camera-off")), Q_ARG(QVariant, QString())));
        QTRY_VERIFY(!camera->changing());
        QCOMPARE(entries().at(3), QString("Could not turn off Logitech Camera. (dim)"));
    }
    // The Printer Selector module (9.2.2's: desktop printers, the default checked, "No Desktop
    // Printers" without any) over stand-in lpstat/lpoptions that keep the default in a file.
    void printerSelectorListsPrintersAndSetsTheDefault()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto script = [&](const QString &name, const QByteArray &body) {
            QFile file(root.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("#!/bin/sh\n" + body);
            file.close();
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
        };
        const auto state = root.filePath("default");
        script("lpstat", ("if [ \"$1\" = -e ]; then [ -f " + root.filePath("none") + " ] || printf 'Laser\\nInkjet\\n'; "
                          "else printf 'system default destination: %s\\n' \"$(cat " + state + ")\"; fi\n").toUtf8());
        script("lpoptions", ("[ \"$1\" = -d ] && printf %s \"$2\" > " + state + "\n").toUtf8());
        QFile initial(state);
        QVERIFY(initial.open(QIODevice::WriteOnly));
        initial.write("Laser");
        initial.close();
        PrinterSelector printers(root.filePath("lpstat"), root.filePath("lpoptions"));
        QVERIFY(printers.printers().isEmpty());
        printers.refresh();
        QTRY_COMPARE(printers.printers().size(), 2);
        QTRY_VERIFY(!printers.busy());
        QCOMPARE(printers.printers().at(0).toMap().value("name").toString(), QString("Laser"));
        QVERIFY(printers.printers().at(0).toMap().value("default").toBool());
        QVERIFY(!printers.setDefault("Missing"));
        QVERIFY(printers.setDefault("Inkjet"));
        QTRY_VERIFY(!printers.busy() && printers.printers().at(1).toMap().value("default").toBool());
        QVERIFY(!printers.printers().at(0).toMap().value("default").toBool());
        // Missing CUPS: no printers, no error.
        PrinterSelector absent(root.filePath("no-lpstat"), root.filePath("no-lpoptions"));
        absent.refresh();
        QTRY_VERIFY(!absent.busy());
        QVERIFY(absent.printers().isEmpty());
        // In the desktop: the strip's menu.
        QByteArray previous[2] = {qgetenv("FRANKENSTEIN_LPSTAT"), qgetenv("FRANKENSTEIN_LPOPTIONS")};
        const auto restore = qScopeGuard([&] {
            qputenv("FRANKENSTEIN_LPSTAT", previous[0]);
            qputenv("FRANKENSTEIN_LPOPTIONS", previous[1]);
        });
        qputenv("FRANKENSTEIN_LPSTAT", root.filePath("lpstat").toUtf8());
        qputenv("FRANKENSTEIN_LPOPTIONS", root.filePath("lpoptions").toUtf8());
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.filePath("ws"), root.filePath("desktop-profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QTRY_VERIFY(visualItem(view.contentItem(), "control-strip-printer"));
        const auto entries = [&] {
            QVariant result;
            QMetaObject::invokeMethod(view.rootObject(), "stripMenuEntries", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QString("printer")));
            return result.toList();
        };
        auto *selector = qobject_cast<PrinterSelector *>(manager.printers());
        QVERIFY(selector);
        selector->refresh();
        QTRY_VERIFY(!selector->busy() && entries().size() == 2);
        QCOMPARE(entries().at(0).toMap().value("text").toString(), QString("Laser"));
        QVERIFY(entries().at(1).toMap().value("checked").toBool()); // Inkjet, set above.
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QString("default-printer")), Q_ARG(QVariant, QString("Laser"))));
        QTRY_VERIFY(!selector->busy() && entries().at(0).toMap().value("checked").toBool());
        QFile none(root.filePath("none"));
        QVERIFY(none.open(QIODevice::WriteOnly));
        none.close();
        selector->refresh();
        QTRY_VERIFY(!selector->busy() && entries().size() == 1);
        QCOMPARE(entries().at(0).toMap().value("text").toString(), QString("No Desktop Printers"));
        QVERIFY(!entries().at(0).toMap().value("enabled").toBool());
    }
    // The Keychain module over a real Secret Service. Run under a private bus with an unlocked
    // throwaway GNOME Keyring (scripts/test-keychain.sh); skipped anywhere else. The package
    // build's check() has the real HOME: there the session bus started the user's own keyring,
    // which this test would have locked (update 43's build, 2026-11-09).
    void keychainModuleLocksKeychainsByName()
    {
        if (qEnvironmentVariable("PLATINUM_THROWAWAY_KEYRING") != "1")
            QSKIP("Only against scripts/test-keychain.sh's throwaway keyring");
        KeychainService keychains(QDBusConnection::sessionBus());
        keychains.refresh();
        QTest::qWait(1500);
        if (!keychains.available() || keychains.keychains().isEmpty())
            QSKIP("No Secret Service with a keychain on this session bus");
        QTRY_VERIFY(!keychains.keychains().isEmpty());
        const auto first = keychains.keychains().first().toMap();
        QVERIFY(!first.value("label").toString().isEmpty());
        QVERIFY2(!first.value("locked").toBool(), "the test keyring starts unlocked");
        const auto path = first.value("path").toString();
        keychains.lock(path);
        QTRY_VERIFY_WITH_TIMEOUT(keychains.keychains().first().toMap().value("locked").toBool(), 5000);
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.filePath("ws"), root.filePath("desktop-profile"), {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        auto *service = qobject_cast<KeychainService *>(manager.keychains());
        QVERIFY(service);
        service->refresh();
        const auto entries = [&] {
            QVariant result;
            QMetaObject::invokeMethod(view.rootObject(), "stripMenuEntries", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QString("keychain")));
            return result.toList();
        };
        const auto label = first.value("label").toString();
        QTRY_COMPARE(entries().first().toMap().value("text").toString(), QString("Unlock “%1”…").arg(label));
        QVERIFY(!entries().last().toMap().value("enabled").toBool()); // Nothing left to lock.
        QCOMPARE(entries().last().toMap().value("text").toString(), QString("Lock All Keychains"));
    }
    void monitorsPanelTurnsDisplaysOnAndOffAndAsksToKeepTheChange()
    {
        // A managed session (every real login): each connected display has an On box that takes
        // effect at once through the supervisor, which then waits for Keep (Revert is the default
        // button and the supervisor's own 15-second timeout). One display always stays on.
        QTemporaryDir root;
        const auto control = root.filePath("control");
        QLocalServer supervisor;
        QVERIFY(supervisor.listen(control));
        QByteArrayList commands;
        connect(&supervisor, &QLocalServer::newConnection, &supervisor, [&] {
            auto *client = supervisor.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, client, [&, client] {
                if (!client->canReadLine())
                    return;
                commands.append(client->readLine());
                client->write(QJsonDocument(QJsonObject{{"ok", true}}).toJson(QJsonDocument::Compact));
                client->flush();
                client->disconnectFromServer();
            });
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        });
        qputenv("PLATINUM_SESSION_CONTROL", control.toLocal8Bit());
        const auto unset = qScopeGuard([] { qunsetenv("PLATINUM_SESSION_CONTROL"); });
        QQuickView view;
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(1920, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto one = *DisplayLayout::create({{"DP-3", QRect(0, 0, 1920, 760), 1, "normal"}}, "DP-3",
                                                DisplayLayout::Mode::Extend);
        const auto two = *DisplayLayout::create({{"DP-3", QRect(0, 0, 1120, 760), 1.5, "normal"},
                                                 {"eDP-1", QRect(1120, 0, 800, 500), 2, "normal"}},
                                                "DP-3", DisplayLayout::Mode::Extend);
        const auto details = [](bool builtInOn, bool pending) {
            DisplayDetails d;
            d.managed = true;
            d.labels = builtInOn ? QVariantMap{{"DP-3", "PM1561P"}, {"eDP-1", "eDP-1"}} : QVariantMap{{"DP-3", "PM1561P"}};
            if (!builtInOn)
                d.available = {QVariantMap{{"name", "eDP-1"}, {"label", "eDP-1"}, {"lidClosed", false}}};
            d.available.append(QVariantMap{{"name", "HDMI-A-1"}, {"label", "Samsung"}, {"lidClosed", true}});
            d.pending = pending;
            const auto mode = [](int w, int h, int r) { return QVariantMap{{"width", w}, {"height", h}, {"refresh", r}}; };
            d.modes.insert("DP-3", QVariantMap{{"modes", QVariantList{mode(1920, 1080, 60000), mode(1680, 1050, 59954),
                                                                     mode(1280, 720, 60000)}},
                                               {"mode", mode(1920, 1080, 60000)}});
            return d;
        };
        manager.setDisplayLayout(one);
        manager.setDisplayDetails(details(false, false));
        QVERIFY(manager.displaySwitchingAvailable());
        auto *panel = view.rootObject()->findChild<QObject *>("monitors-panel");
        auto *keep = view.rootObject()->findChild<QObject *>("monitors-keep-dialog");
        QVERIFY(panel && keep);
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
        QQuickItem *external = nullptr, *builtIn = nullptr, *shut = nullptr;
        QTRY_VERIFY((external = item("monitors-on-DP-3")) && (builtIn = item("monitors-on-eDP-1")) &&
                    (shut = item("monitors-on-HDMI-A-1")));
        QCOMPARE(external->property("text").toString(), QString("PM1561P"));
        QCOMPARE(builtIn->property("text").toString(), QString("Built-in"));
        QVERIFY(external->property("checked").toBool() && !builtIn->property("checked").toBool());
        QVERIFY(!external->isEnabled()); // The only display that is on stays on.
        QVERIFY(!shut->isEnabled() && shut->property("text").toString().endsWith("(the lid is closed)"));
        QVERIFY(!item("monitors-use-all")->isVisible()); // The On boxes replace the next-login choice.
        QCOMPARE(item("monitors-unavailable")->property("text").toString(),
                 QString("Only one display is on. To use another one, turn it on below."));
        // Turning the built-in display on: a request at once, the box follows only the published layout.
        click(builtIn);
        QTRY_COMPARE(commands.size(), 1);
        QCOMPARE(commands.last(), QByteArray("display eDP-1 on\n"));
        QVERIFY(!builtIn->property("checked").toBool());
        QTRY_VERIFY(!manager.displayRequestBusy());
        manager.setDisplayLayout(two);
        manager.setDisplayDetails(details(true, true));
        QTRY_VERIFY(keep->property("opened").toBool());
        QTRY_VERIFY(item("monitors-keep-text")->property("text").toString().contains("15 seconds"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/monitors-keep.png"));
        QTRY_VERIFY(item("monitors-keep-text")->property("text").toString().contains("14 seconds"));
        click(item("monitors-keep-dialog-accept"));
        QTRY_COMPARE(commands.size(), 2);
        QCOMPARE(commands.last(), QByteArray("display keep\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        manager.setDisplayDetails(details(true, false));
        QTRY_VERIFY(!keep->property("visible").toBool());
        QTRY_VERIFY(panel->property("opened").toBool()); // Still there to change more.
        QTRY_VERIFY((external = item("monitors-on-DP-3")) && (builtIn = item("monitors-on-eDP-1")));
        QVERIFY(builtIn->property("checked").toBool() && external->isEnabled());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/monitors-on.png"));
        // The main display's resolution, rotation and size, each one request at once.
        QCOMPARE(item("monitors-settings-title")->property("text").toString(), QString("Resolution of PM1561P:"));
        QQuickItem *slower = nullptr;
        QTRY_VERIFY((slower = item("monitors-resolution-1")));
        QCOMPARE(slower->childItems().first()->property("text").toString(), QString("1680 × 1050, 59.95 Hz"));
        click(slower);
        QTRY_COMPARE(commands.size(), 3);
        QCOMPARE(commands.last(), QByteArray("display DP-3 mode 1680x1050@59954\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        click(item("monitors-rotate-90"));
        QTRY_COMPARE(commands.size(), 4);
        QCOMPARE(commands.last(), QByteArray("display DP-3 rotate 90\n"));
        QVERIFY(item("monitors-rotate-normal")->property("checked").toBool()); // Until the supervisor publishes it.
        QTRY_VERIFY(!manager.displayRequestBusy());
        click(item("monitors-scale-200"));
        QTRY_COMPARE(commands.size(), 5);
        QCOMPARE(commands.last(), QByteArray("display DP-3 scale 2.00\n"));
        QVERIFY(item("monitors-scale-150")->property("checked").toBool());
        QTRY_VERIFY(!manager.displayRequestBusy());
        // The built-in display's settings after clicking it in the arrangement.
        click(item("monitors-display-eDP-1"));
        QTRY_COMPARE(item("monitors-settings-title")->property("text").toString(), QString("Resolution of Built-in:"));
        QVERIFY(item("monitors-scale-200")->property("checked").toBool());
        // The Control Strip's Monitor Resolution module, as 9.2.2's: Open Monitors Control Panel, a
        // line, each display's name over its resolutions with a bullet on the current one.
        QVariant resolution;
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "stripMenuEntries", Q_RETURN_ARG(QVariant, resolution),
                                          Q_ARG(QVariant, QString("resolution"))));
        const auto rows = resolution.toList();
        QCOMPARE(rows.at(0).toMap().value("text").toString(), QString("Open Monitors Control Panel"));
        QVERIFY(rows.at(1).toMap().value("separator").toBool());
        QVariantMap heading, chosen;
        int bullets = 0;
        for (const auto &row : rows) {
            const auto m = row.toMap();
            if (m.value("name") == "resolution-display-DP-3") heading = m;
            if (m.value("bullet").toBool()) ++bullets;
            if (m.value("name") == "resolution-DP-3-1280x720@60000") chosen = m;
        }
        QVERIFY(heading.value("heading").toBool() && !heading.value("enabled").toBool());
        QCOMPARE(heading.value("text").toString(), QString("PM1561P"));
        QCOMPARE(bullets, 1); // The current mode; only DP-3 lists its modes here.
        QCOMPARE(chosen.value("text").toString(), QString("1280 x 720, 60Hz"));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand", Q_ARG(QVariant, chosen.value("action")),
                                          Q_ARG(QVariant, chosen.value("id"))));
        QTRY_COMPARE(commands.size(), 6);
        QCOMPARE(commands.last(), QByteArray("display DP-3 mode 1280x720@60000\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        commands.removeLast();
        QVERIFY(!manager.setDisplayMode("DP-3", 640, 480, 60000)); // Not one the monitor offers.
        QVERIFY(!manager.setDisplayRotation("DP-3", "flipped"));
        QVERIFY(!manager.setDisplayScale("DP-3", 9));
        QCOMPARE(commands.size(), 5);
        // Arrange: drag the built-in display to the left of the external one. It lands flush against
        // its left side with the tops lined up, and only the supervisor's layout moves it.
        QQuickItem *external3 = nullptr, *builtIn3 = nullptr;
        QTRY_VERIFY((external3 = item("monitors-display-DP-3")) && (builtIn3 = item("monitors-display-eDP-1")));
        const auto from = builtIn3->mapToScene(QPointF(builtIn3->width() / 2, builtIn3->height() / 2)).toPoint();
        const auto to = external3->mapToScene(QPointF(-builtIn3->width() / 2 - 6, builtIn3->height() / 2 + 3)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 10; ++step)
            QTest::mouseMove(&view, from + (to - from) * step / 10, 10);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, to, 10);
        QTRY_COMPARE(commands.size(), 6);
        QCOMPARE(commands.last(), QByteArray("display eDP-1 position -800,0\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        QTRY_VERIFY(qAbs(item("monitors-display-eDP-1")->x() - external3->x() - external3->width()) < 2); // Back until published.
        QVERIFY(!manager.setDisplayPosition("DP-3", 99999, 0));
        QCOMPARE(commands.size(), 6);
        // Mirror displays: one request, then the supervisor's mirrored layout (the main display inside
        // the bigger canvas the built-in display covers) checks the box and keeps the built-in on.
        click(item("monitors-mode-mirror"));
        QTRY_COMPARE(commands.size(), 7);
        QCOMPARE(commands.last(), QByteArray("display mirror on\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        auto mirrored = details(true, false);
        mirrored.labels = QVariantMap{{"DP-3", "PM1561P"}};
        mirrored.mirrors = {QVariantMap{{"name", "eDP-1"}, {"label", "eDP-1"}}};
        manager.setDisplayLayout(*DisplayLayout::create({{"DP-3", QRect(320, 30, 1280, 700), 1.5, "normal"}}, "DP-3",
                                                        DisplayLayout::Mode::Extend, nullptr, QSize(1920, 760)));
        manager.setDisplayDetails(mirrored);
        QTRY_VERIFY(item("monitors-mode-mirror")->property("checked").toBool());
        QTRY_VERIFY(item("monitors-on-eDP-1") && item("monitors-on-eDP-1")->property("checked").toBool());
        QVERIFY(item("monitors-on-DP-3")->isEnabled()); // Two displays are on, so either can go off.
        QCOMPARE(manager.primaryDisplayRect(), QRect(320, 30, 1280, 700));
        QVERIFY(item("monitors-use-all-note")->property("text").toString().startsWith("Every display shows the main display."));
        click(item("monitors-mode-mirror"));
        QTRY_COMPARE(commands.size(), 8);
        QCOMPARE(commands.last(), QByteArray("display mirror off\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        manager.setDisplayLayout(two);
        manager.setDisplayDetails(details(true, false));
        QTRY_VERIFY(!item("monitors-mode-mirror")->property("checked").toBool());
        commands.clear();
        // Turning the main display off, then Return: Revert is the default.
        QTRY_VERIFY((external = item("monitors-on-DP-3")) && external->isEnabled());
        click(external);
        QTRY_COMPARE(commands.size(), 1);
        QCOMPARE(commands.last(), QByteArray("display DP-3 off\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        manager.setDisplayDetails(details(true, true));
        QTRY_VERIFY(keep->property("opened").toBool());
        QTRY_VERIFY(keep->property("holdsFocus").toBool());
        QVERIFY(!item("monitors-on-eDP-1")->isEnabled()); // One change at a time.
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_COMPARE(commands.size(), 2);
        QCOMPARE(commands.last(), QByteArray("display revert\n"));
        QTRY_VERIFY(!manager.displayRequestBusy());
        manager.setDisplayDetails(details(true, false)); // The supervisor publishes the reverted layout.
        // The supervisor's own timeout also closes the question when the change is undone.
        QVERIFY(manager.setDisplayOn("DP-3", false));
        QTRY_COMPARE(commands.size(), 3);
        QTRY_VERIFY(!manager.displayRequestBusy());
        manager.setDisplayDetails(details(true, true));
        QTRY_VERIFY(keep->property("opened").toBool());
        manager.setDisplayDetails(details(true, false));
        QTRY_VERIFY(!keep->property("visible").toBool());
        QTRY_VERIFY(!manager.displayRequestBusy());
        const auto sent = commands.size();
        QVERIFY(!manager.setDisplayOn("VGA-1", true)); // Not connected.
        QVERIFY(!manager.keepDisplayChange());        // Nothing waiting.
        QCOMPARE(commands.size(), sent);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("visible").toBool());
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
        QCOMPARE(view.rootObject()->property("menuAccent").value<QColor>(), QColor("#333399"));

        QVERIFY(open("Appearance"));
        for (const auto *tab : {"appearance", "fonts", "desktop", "sound", "options"})
            QVERIFY2(shown(QString("appearance-tab-") + tab), tab);
        QVERIFY(!shown("appearance-tab-themes")); // One theme only: no Themes tab.
        QVERIFY(shown("appearance-accent-sample"));
        // Each choice applies at once, as in Mac OS 9; there is no OK.
        // 9.2.2's three pop-ups: Appearance (Platinum only), Highlight Color, Variation.
        QVERIFY(choose("appearance-variation"));
        QVERIFY(choose("appearance-variation-graphite"));
        QTRY_COMPARE(settings->accent(), QString("graphite"));
        QCOMPARE(settings->highlight(), QString("Purple"));
        QCOMPARE(settings->highlightColor(), QColor("#ccccff"));
        QVERIFY(choose("appearance-highlight"));
        QVERIFY(choose("appearance-highlight-Gold"));
        QTRY_COMPARE(settings->highlight(), QString("Gold"));
        QCOMPARE(visualItem(view.contentItem(), "appearance-highlight-sample")->property("color").value<QColor>(), QColor("#ffee88"));
        QTRY_VERIFY(!shown("appearance-highlight-Gold")); // The pop-up's menu has closed.
        {
            // Saved; and an unknown colour is refused (on a separate instance: the panel's would alert).
            AppearanceSettings saved(root.path() + "/profile");
            QCOMPARE(saved.highlight(), QString("Gold"));
            QVERIFY(!saved.setHighlight("Chartreuse"));
            QCOMPARE(saved.highlight(), QString("Gold"));
        }
        QCOMPARE(view.rootObject()->property("menuAccent").value<QColor>(), QColor("#656565"));
        QTest::qWait(300); // Evidence only: the tab canvases and wallpaper paint on later frames.
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/appearance-tabs.png"));
        QVERIFY(choose("appearance-tab-fonts"));
        QTRY_VERIFY(shown("appearance-font-large"));
        QVERIFY(!shown("appearance-accent-sample"));
        QVERIFY(choose("appearance-tab-sound"));
        QVERIFY(choose("appearance-tab-options"));
        QTRY_VERIFY(shown("option-double-click-collapse"));
        QVERIFY(choose("keep-screen-awake"));
        QTRY_VERIFY(settings->keepScreenAwake());
        QVERIFY(choose("option-double-click-collapse"));
        QTRY_VERIFY(!settings->doubleClickCollapse());
        QCOMPARE(settings->accent(), QString("graphite")); // Unchanged by the other options.
        AppearanceSettings reloaded(root.path() + "/profile");
        QCOMPARE(reloaded.accent(), QString("graphite"));
        QVERIFY(!reloaded.doubleClickCollapse());
        QVERIFY(reloaded.keepScreenAwake());

        // Closing the window keeps every choice; reopening shows them.
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        QCOMPARE(settings->accent(), QString("graphite"));
        QVERIFY(open("Options"));
        QTRY_VERIFY(shown("keep-screen-awake"));
        QVERIFY(visualItem(view.contentItem(), "keep-screen-awake")->property("checked").toBool());
        QVERIFY(choose("keep-screen-awake"));
        QTRY_VERIFY(!settings->keepScreenAwake());
        AppearanceSettings awakeOff(root.path() + "/profile");
        QVERIFY(!awakeOff.keepScreenAwake());
        QVERIFY(open("Appearance"));
        QVERIFY(choose("appearance-variation"));
        QVERIFY(choose("appearance-variation-blue"));
        QTRY_COMPARE(settings->accent(), QString("blue"));

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
        QCOMPARE(visualItem(view.contentItem(), "control-panels-heading")->property("text").toString(), QString("Desktop Picture"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!chooser->property("opened").toBool());
    }
    void dateTimePanelSetsTheMenuBarClockAndShowsTheCalendar()
    {
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
        const auto item = [&](const QString &name) {
            view.grabWindow();
            return visualItem(view.contentItem(), name);
        };
        const auto text = [&](const QString &name) { return item(name)->property("text").toString(); };
        const auto click = [&](const QString &name) {
            auto *target = item(name);
            if (!target || !target->isVisible() || !target->isEnabled())
                return false;
            QTest::mouseClick(&view, Qt::LeftButton, {}, target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint());
            return true;
        };
        const auto matches = [](const QString &value, const char *pattern) {
            return QRegularExpression(QString("^") + pattern + "$").match(value).hasMatch();
        };
        // Fixed times, whatever the hour of the run: 12-hour unless chosen (2 PM is 2, midnight 12).
        const auto clockAt = [&](const QTime &time, const QVariantMap &options) {
            QVariant text;
            QMetaObject::invokeMethod(view.rootObject(), "clockText", Q_RETURN_ARG(QVariant, text),
                                      Q_ARG(QVariant, QDateTime(QDate(2026, 11, 2), time)),
                                      Q_ARG(QVariant, options), Q_ARG(QVariant, false));
            return text.toString();
        };
        QCOMPARE(clockAt(QTime(14, 5, 9), {}), QString("2:05 PM"));
        QCOMPARE(clockAt(QTime(0, 30), {}), QString("12:30 AM"));
        QCOMPARE(clockAt(QTime(12, 0), {}), QString("12:00 PM"));
        QCOMPARE(clockAt(QTime(14, 5, 9), {{"seconds", true}}), QString("2:05:09 PM"));
        QCOMPARE(clockAt(QTime(14, 5, 9), {{"hours24", true}, {"seconds", true}}), QString("14:05:09"));
        QCOMPARE(clockAt(QTime(9, 5), {{"hours24", true}}), QString("09:05"));
        // The clock as before: 12-hour, minutes, nothing else.
        QVERIFY2(matches(text("menu-bar-clock"), "\\d{1,2}:\\d{2} (AM|PM)"), qPrintable(text("menu-bar-clock")));
        // Only the known options (on a separate copy: the desktop would show the error).
        AppearanceSettings separate(root.path() + "/separate");
        QVERIFY(!separate.setClockOption("blink", true));

        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                          Q_ARG(QVariant, QVariant(QString("date-time"))), Q_ARG(QVariant, QVariant(0))));
        auto *panel = view.rootObject()->findChild<QObject *>("date-time-panel");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QCOMPARE(text("control-panels-heading"), QString("Date & Time"));
        QVERIFY(text("date-time-today").contains(QString::number(QDate::currentDate().year())));
        QVERIFY(text("date-time-zone").startsWith("Time zone: "));
        // The calendar pages by month.
        const auto month = text("date-time-month");
        QCOMPARE(month, QLocale().toString(QDate::currentDate(), "MMMM yyyy"));
        QVERIFY(click("date-time-next"));
        QTRY_COMPARE(text("date-time-month"), QLocale().toString(QDate::currentDate().addMonths(1), "MMMM yyyy"));
        QVERIFY(click("date-time-previous"));
        QTRY_COMPARE(text("date-time-month"), month);

        // Each option applies at once to the menu bar and is saved.
        QVERIFY(click("clock-hours24"));
        QTRY_VERIFY(settings->clock().value("hours24").toBool());
        QTRY_VERIFY2(matches(text("menu-bar-clock"), "\\d{2}:\\d{2}"), qPrintable(text("menu-bar-clock")));
        QVERIFY(click("clock-seconds"));
        QTRY_VERIFY2(matches(text("menu-bar-clock"), "\\d{2}:\\d{2}:\\d{2}"), qPrintable(text("menu-bar-clock")));
        QVERIFY(click("clock-weekday"));
        QTRY_VERIFY(text("menu-bar-clock").startsWith(QLocale().toString(QDate::currentDate(), "ddd")));
        // The panel's sample is the menu bar's own text.
        QTRY_COMPARE(text("date-time-sample"), text("menu-bar-clock"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/date-time-panel.png"));
        AppearanceSettings reloaded(root.path() + "/profile");
        QVERIFY(reloaded.clock().value("hours24").toBool());
        QVERIFY(reloaded.clock().value("seconds").toBool());
        QVERIFY(reloaded.clock().value("weekday").toBool());
        QVERIFY(!reloaded.clock().value("date").toBool());
        // Hiding the clock greys the other options.
        QVERIFY(click("clock-visible"));
        QTRY_VERIFY(!item("menu-bar-clock")->isVisible());
        QVERIFY(!item("clock-seconds")->isEnabled());
        QVERIFY(click("clock-visible"));
        QTRY_VERIFY(item("menu-bar-clock")->isVisible());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());

        // Clicking the clock shows today's date, then the time again.
        QVERIFY(click("menu-bar-clock"));
        QTRY_COMPARE(text("menu-bar-clock"), QLocale().toString(QDate::currentDate(), "dddd, MMMM d, yyyy"));
        QVERIFY(click("menu-bar-clock"));
        QTRY_VERIFY(text("menu-bar-clock").startsWith(QLocale().toString(QDate::currentDate(), "ddd") + " "));
    }
    void keyboardMouseAndEnergyPanelsSaveSettingsAndAskTheSupervisorToApplyThem()
    {
        // A stand-in session supervisor: records each request and answers ok.
        QTemporaryDir root;
        const auto control = root.filePath("control");
        QLocalServer supervisor;
        QVERIFY(supervisor.listen(control));
        QByteArrayList commands;
        connect(&supervisor, &QLocalServer::newConnection, &supervisor, [&] {
            auto *client = supervisor.nextPendingConnection();
            connect(client, &QLocalSocket::readyRead, client, [&, client] {
                if (!client->canReadLine())
                    return;
                commands.append(client->readLine());
                client->write(QJsonDocument(QJsonObject{{"ok", true}}).toJson(QJsonDocument::Compact));
                client->flush();
                client->disconnectFromServer();
            });
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
        });
        qputenv("PLATINUM_SESSION_CONTROL", control.toLocal8Bit());
        const auto unset = qScopeGuard([] { qunsetenv("PLATINUM_SESSION_CONTROL"); });
        QQuickView view;
        view.resize(1120, 760);
        const auto profile = root.path() + "/profile";
        NativeManager manager(nullptr, &view, root.path(), profile, {});
        auto *input = qobject_cast<InputSettings *>(manager.input());
        QVERIFY(input);
        // Nothing chosen: the defaults, and no file.
        QCOMPARE(input->keyboard().value("repeatRate").toInt(), 25);
        QCOMPARE(input->keyboard().value("repeatDelay").toInt(), 600);
        QVERIFY(input->touchpad().value("tap").toBool());
        QVERIFY(!QFile::exists(profile + "/input.json"));
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) {
            view.grabWindow();
            return visualItem(view.contentItem(), name);
        };
        const auto click = [&](const QString &name) {
            auto *target = item(name);
            if (!target || !target->isVisible() || !target->isEnabled())
                return false;
            QTest::mouseClick(&view, Qt::LeftButton, {}, target->mapToScene(QPointF(10, target->height() / 2)).toPoint());
            return true;
        };
        const auto saved = [&] {
            QFile file(profile + "/input.json");
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        };
        // Each finished change asks once; the supervisor rereads the file, so the last one wins.
        // (QTRY_VERIFY evaluates its condition once more at the end: keep it free of side effects.)
        int applied = 0;
        const auto applies = [&] { return int(commands.count("input apply\n")); };
        const auto dispatch = [&](const QString &action) {
            return QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                             Q_ARG(QVariant, QVariant(action)), Q_ARG(QVariant, QVariant(0)));
        };

        // Keyboard: the arrow keys move the slider; the value is saved once, then applied.
        QVERIFY(dispatch("keyboard"));
        auto *keyboard = view.rootObject()->findChild<QObject *>("keyboard-panel");
        QTRY_VERIFY(keyboard && keyboard->property("opened").toBool());
        auto *rate = item("keyboard-repeat-rate");
        QVERIFY(rate);
        rate->forceActiveFocus();
        for (int step = 0; step < 5; ++step)
            QTest::keyClick(&view, Qt::Key_Right);
        QTRY_COMPARE(input->keyboard().value("repeatRate").toInt(), 30);
        QCOMPARE(saved().value("keyboard").toObject().value("repeatRate").toInt(), 30);
        QVERIFY(!saved().value("keyboard").toObject().contains("repeatDelay")); // Only what was chosen.
        QTRY_VERIFY(applies() > applied);
        applied = applies();
        auto *delay = item("keyboard-repeat-delay");
        delay->forceActiveFocus();
        QTest::keyClick(&view, Qt::Key_Right); // Shorter: 600 ms to 550 ms.
        QTRY_COMPARE(input->keyboard().value("repeatDelay").toInt(), 550);
        QTRY_VERIFY(applies() > applied);
        applied = applies();
        QCOMPARE(item("keyboard-repeat-summary")->property("text").toString(),
                 QString("Repeats 30 times a second, after 0.55 seconds."));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/keyboard-panel.png"));
        QVERIFY(click("keyboard-defaults"));
        QTRY_COMPARE(input->keyboard().value("repeatRate").toInt(), 25);
        QVERIFY(!saved().contains("keyboard"));
        QTRY_VERIFY(applies() > applied);
        applied = applies();

        // Keyboard Layouts: U.S. alone, checked and dimmed; no keyboard menu.
        auto *keyCaps = qobject_cast<KeyCaps *>(manager.keyCaps());
        QVERIFY(keyCaps);
        QCOMPARE(input->layout(), QString("us"));
        QCOMPARE(keyCaps->property("layout").toString(), QString("us"));
        auto *keyboardMenu = view.rootObject()->findChild<QQuickItem *>("keyboard-menu");
        QVERIFY(keyboardMenu && !keyboardMenu->isVisible());
        auto *layoutList = item("keyboard-layouts");
        QVERIFY(layoutList);
        const auto showLayout = [&](const QString &id) {
            const auto choices = input->layoutChoices();
            const auto at = std::find_if(choices.cbegin(), choices.cend(),
                                         [&](const QVariant &c) { return c.toMap().value("id") == id; });
            return QMetaObject::invokeMethod(layoutList, "positionViewAtIndex", Q_ARG(int, int(at - choices.cbegin())),
                                             Q_ARG(int, 1 /* ListView.Beginning */));
        };
        QVERIFY(showLayout("us"));
        QTRY_VERIFY(item("keyboard-layout-us"));
        QVERIFY(item("keyboard-layout-us")->property("checked").toBool());
        QVERIFY(!item("keyboard-layout-us")->isEnabled());
        QVERIFY(showLayout("de"));
        QTRY_VERIFY(item("keyboard-layout-de") && item("keyboard-layout-de")->isVisible());
        QVERIFY(click("keyboard-layout-de"));
        QTRY_COMPARE(input->layouts(), QStringList({"de", "us"})); // In the list's order.
        QCOMPARE(input->layout(), QString("us"));
        QCOMPARE(saved().value("keyboard").toObject().value("layouts").toArray().size(), 2);
        QTRY_VERIFY(applies() > applied);
        applied = applies();
        QTRY_VERIFY(keyboardMenu->isVisible());
        QCOMPARE(item("keyboard-menu-title")->property("text").toString(), QString("US"));
        QCOMPARE(item("keyboard-layout-in-use")->property("text").toString(), QString("In use: U.S."));
        // The keyboard menu lists the checked layouts, the one in use checked; choosing one uses it.
        QTest::mouseClick(&view, Qt::LeftButton, {}, keyboardMenu->mapToScene(QPointF(keyboardMenu->width() / 2, 12)).toPoint());
        QTRY_VERIFY(item("desktop-command-keyboard-layout-de"));
        QVERIFY(item("desktop-command-keyboard-layout-us"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/keyboard-menu.png"));
        QVERIFY(click("desktop-command-keyboard-layout-de"));
        QTRY_COMPARE(input->layout(), QString("de"));
        QCOMPARE(keyCaps->property("layout").toString(), QString("de"));
        QTRY_COMPARE(item("keyboard-menu-title")->property("text").toString(), QString("DE"));
        QTRY_VERIFY(applies() > applied);
        applied = applies();
        // Command-Option-Space moves to the next one, wrapping.
        QTest::keyClick(&view, Qt::Key_Space, Qt::MetaModifier | Qt::AltModifier);
        QTRY_COMPARE(input->layout(), QString("us"));
        // Unchecking the layout in use moves to the one left; Use Defaults keeps the list.
        QVERIFY(showLayout("us"));
        QTRY_VERIFY(item("keyboard-layout-us") && item("keyboard-layout-us")->isVisible());
        QVERIFY(click("keyboard-layout-us"));
        QTRY_COMPARE(input->layouts(), QStringList{"de"});
        QCOMPARE(input->layout(), QString("de"));
        QTRY_VERIFY(!keyboardMenu->isVisible());
        QVERIFY(click("keyboard-defaults"));
        QCOMPARE(input->layout(), QString("de"));
        // Refused on a second reader, whose errors raise no alert on this desktop.
        InputSettings other(profile);
        QCOMPARE(other.layout(), QString("de"));
        QVERIFY(!other.set("keyboard", "layout", "us")); // Not checked.
        QVERIFY(!other.set("keyboard", "layouts", QVariantList{"de", "xx"}));
        QVERIFY(!other.set("keyboard", "layouts", QVariantList{}));
        QVERIFY(input->set("keyboard", "layouts", QVariantList{"us"}));
        QCOMPARE(input->layout(), QString("us"));
        QTRY_VERIFY(applies() > applied);
        applied = applies();

        // Mouse & Trackpad: each box applies at once.
        QVERIFY(dispatch("mouse"));
        auto *mouse = view.rootObject()->findChild<QObject *>("mouse-panel");
        QTRY_VERIFY(mouse && mouse->property("opened").toBool());
        QCOMPARE(item("control-panels-heading")->property("text").toString(), QString("Mouse & Trackpad"));
        QVERIFY(click("mouse-natural"));
        QTRY_VERIFY(input->pointer().value("natural").toBool());
        QVERIFY(click("trackpad-tap"));
        QTRY_VERIFY(!input->touchpad().value("tap").toBool());
        QCOMPARE(saved().value("touchpad").toObject().value("tap"), QJsonValue(false));
        auto *speed = item("mouse-speed");
        speed->forceActiveFocus();
        QTest::keyClick(&view, Qt::Key_Left);
        QTest::keyClick(&view, Qt::Key_Left);
        QTRY_COMPARE(input->pointer().value("speed").toDouble(), -0.2);
        QTRY_VERIFY(applies() > applied);
        applied = applies();
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/mouse-panel.png"));
        // A later session reads the choices back; out-of-range values are refused.
        InputSettings reloaded(profile);
        QVERIFY(reloaded.pointer().value("natural").toBool());
        QCOMPARE(reloaded.pointer().value("speed").toDouble(), -0.2);
        QVERIFY(!reloaded.touchpad().value("tap").toBool());
        QVERIFY(!reloaded.set("keyboard", "repeatRate", 0));
        QVERIFY(!reloaded.set("pointer", "speed", 3));
        QVERIFY(!reloaded.set("pointer", "natural", 1));
        QVERIFY(!reloaded.set("pointer", "fast", true));
        QVERIFY(click("mouse-defaults"));
        QTRY_VERIFY(input->touchpad().value("tap").toBool());
        QVERIFY(!input->pointer().value("natural").toBool());
        // Only the checked Keyboard Layouts (U.S. checked above) are left.
        QTRY_COMPARE(saved().keys(), QStringList({"keyboard", "version"}));
        QCOMPARE(saved().value("keyboard").toObject().keys(), QStringList({"layout", "layouts"}));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!mouse->property("opened").toBool());

        // Energy Saver: Never by default; one step left of Never is the longest time.
        auto *energy = qobject_cast<EnergySettings *>(manager.energy());
        QVERIFY(energy);
        QCOMPARE(energy->sleep(), 0);
        QCOMPARE(energy->displayOff(), 0);
        QVERIFY(dispatch("energy"));
        auto *energyPanel = view.rootObject()->findChild<QObject *>("energy-panel");
        QTRY_VERIFY(energyPanel && energyPanel->property("opened").toBool());
        QCOMPARE(item("energy-sleep-label")->property("text").toString(),
                 QString("Put the computer to sleep whenever it’s inactive for: Never"));
        QVERIFY(!item("energy-sleep-now")->isEnabled()); // No power control outside the session.
        auto *sleep = item("energy-sleep");
        sleep->forceActiveFocus();
        QTest::keyClick(&view, Qt::Key_Left);
        QTRY_COMPARE(energy->sleep(), 240);
        QTRY_VERIFY(commands.contains("energy apply\n"));
        QCOMPARE(item("energy-sleep-label")->property("text").toString(),
                 QString("Put the computer to sleep whenever it’s inactive for: 4 hours"));
        auto *display = item("energy-display");
        display->forceActiveFocus();
        for (int step = 0; step < 4; ++step)
            QTest::keyClick(&view, Qt::Key_Left);
        QTRY_COMPARE(energy->displayOff(), 45);
        QTRY_COMPARE(item("energy-display-label")->property("text").toString(),
                     QString("Turn the display off when inactive for: 45 minutes"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/energy-panel.png"));
        QFile file(profile + "/energy.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto stored = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(stored.value("sleep").toInt(), 240);
        QCOMPARE(stored.value("displayOff").toInt(), 45);
        EnergySettings reloaded2(profile);
        QCOMPARE(reloaded2.sleep(), 240);
        QVERIFY(!reloaded2.set("sleep", 2));
        QVERIFY(!reloaded2.set("displayOff", 500));
        QVERIFY(!reloaded2.set("brightness", 5));
        // Command-W closes the window, as any window.
        QTest::keyClick(&view, Qt::Key_W, Qt::MetaModifier);
        QTRY_VERIFY(!energyPanel->property("opened").toBool());
    }
    void stickyNotesAreMadeTypedInSavedRolledUpAndDeleted()
    {
        QTemporaryDir root;
        QQuickView view;
        view.resize(1120, 760);
        const auto profile = root.path() + "/profile";
        NativeManager manager(nullptr, &view, root.path(), profile, {});
        auto *notes = qobject_cast<StickyNotes *>(manager.stickies());
        QVERIFY(notes);
        QCOMPARE(notes->count(), 0);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto item = [&](const QString &name) {
            view.grabWindow();
            return visualItem(view.contentItem(), name);
        };
        const auto at = [&](QQuickItem *target, qreal x = -1, qreal y = -1) {
            return target->mapToScene(QPointF(x < 0 ? target->width() / 2 : x, y < 0 ? target->height() / 2 : y)).toPoint();
        };
        const auto dispatch = [&](const QString &action) {
            return QMetaObject::invokeMethod(view.rootObject(), "dispatchShellCommand",
                                             Q_ARG(QVariant, QVariant(action)), Q_ARG(QVariant, QVariant(0)));
        };
        // Apple menu ▸ Stickies ▸ New Note: a yellow note with the keyboard, ready for typing.
        QVERIFY(dispatch("sticky-new"));
        QTRY_COMPARE(notes->count(), 1);
        const auto id = notes->data(notes->index(0), StickyNotes::IdRole).toInt();
        const auto name = QString::number(id);
        QQuickItem *note = nullptr;
        QTRY_VERIFY((note = item("sticky-note-" + name)) && note->isVisible());
        auto *text = item("sticky-text-" + name);
        QTRY_VERIFY(text->hasActiveFocus());
        QVERIFY(!manager.shellModal());
        for (const char c : QByteArray("buy milk"))
            QTest::keyClick(&view, c);
        QTest::keyClick(&view, Qt::Key_Return);
        for (const char c : QByteArray("call mom"))
            QTest::keyClick(&view, c);
        QTRY_COMPARE(notes->text(id), QString("buy milk\ncall mom"));
        // Saved a moment after typing stops.
        const auto saved = [&] {
            QFile file(profile + "/stickies.json");
            return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
        };
        QTRY_COMPARE(saved().value("notes").toArray().at(0).toObject().value("text").toString(), QString("buy milk\ncall mom"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() + "/artifacts/sticky-note.png"));

        // Right-click for colors and text sizes.
        QTest::mouseClick(&view, Qt::RightButton, {}, at(text, 20, 10));
        QQuickItem *blue = nullptr;
        QTRY_VERIFY((blue = item("sticky-color-blue")) && blue->isVisible());
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(blue));
        QTRY_COMPARE(notes->data(notes->index(0), StickyNotes::ColorRole).toString(), QString("blue"));
        QTest::mouseClick(&view, Qt::RightButton, {}, at(text, 20, 10));
        QQuickItem *large = nullptr;
        QTRY_VERIFY((large = item("sticky-size-large")) && large->isVisible());
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(large));
        QTRY_COMPARE(notes->data(notes->index(0), StickyNotes::TextSizeRole).toString(), QString("large"));

        // Drag the strip to move; drag the corner to resize; both are saved.
        auto *strip = item("sticky-strip-" + name);
        const auto from = at(strip, 60, 7);
        const auto beforeMove=note->position();
        QTest::mousePress(&view, Qt::LeftButton, {}, from);
        QTRY_VERIFY(manager.windowMoveActive());
        for (int step = 1; step <= 5; ++step)
            QTest::mouseMove(&view, from + QPoint(step * 20, step * 10));
        QCOMPARE(note->position(),beforeMove);
        QCOMPARE(notes->data(notes->index(0), StickyNotes::XRole).toInt(),qRound(beforeMove.x()));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, from + QPoint(100, 50));
        QTRY_VERIFY(!manager.windowMoveActive());
        QCOMPARE(note->position(),beforeMove+QPointF(100,50));
        QTRY_COMPARE(notes->data(notes->index(0), StickyNotes::XRole).toInt(), qRound(note->x()));
        const auto oldX = qRound(note->x());
        const auto committedNote=note->position();
        const auto cancelFrom=at(strip,60,7);
        QTest::mousePress(&view,Qt::LeftButton,{},cancelFrom);
        QTest::mouseMove(&view,cancelFrom+QPoint(40,20),30);
        QTRY_VERIFY(manager.windowMoveActive());
        QTest::keyClick(&view,Qt::Key_Escape);
        QTRY_VERIFY(!manager.windowMoveActive());
        QTest::mouseRelease(&view,Qt::LeftButton,{},cancelFrom+QPoint(40,20));
        QCOMPARE(note->position(),committedNote);
        QCOMPARE(notes->data(notes->index(0), StickyNotes::XRole).toInt(),oldX);
        QCOMPARE(notes->count(),1);
        auto *grow = item("sticky-grow-" + name);
        const auto corner = at(grow);
        QTest::mousePress(&view, Qt::LeftButton, {}, corner);
        for (int step = 1; step <= 4; ++step)
            QTest::mouseMove(&view, corner + QPoint(step * 20, step * 15));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, corner + QPoint(80, 60));
        QTRY_COMPARE(notes->data(notes->index(0), StickyNotes::WidthRole).toInt(), 300);
        QCOMPARE(notes->data(notes->index(0), StickyNotes::HeightRole).toInt(), 220);
        QCOMPARE(qRound(note->x()), oldX);

        // Double-clicking the strip rolls the note up to it, its first line showing; again, down.
        QTest::mouseDClick(&view, Qt::LeftButton, {}, at(strip, 60, 7));
        QTRY_VERIFY(notes->data(notes->index(0), StickyNotes::CollapsedRole).toBool());
        QTRY_COMPARE(qRound(note->height()), 16);
        QVERIFY(item("sticky-title-" + name)->isVisible());
        QCOMPARE(item("sticky-title-" + name)->property("text").toString(), QString("buy milk"));
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(item("sticky-shade-" + name)));
        QTRY_COMPARE(qRound(note->height()), 220);

        // A later session finds the note as it was left.
        QVERIFY(notes->saveNow());
        StickyNotes reloaded(profile);
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(reloaded.text(id), QString("buy milk\ncall mom"));
        QCOMPARE(reloaded.data(reloaded.index(0), StickyNotes::ColorRole).toString(), QString("blue"));
        QCOMPARE(reloaded.data(reloaded.index(0), StickyNotes::WidthRole).toInt(), 300);
        QVERIFY(!reloaded.set(id, "color", "plaid"));

        // Hide All Notes keeps them; Show All Notes brings them back.
        QVERIFY(dispatch("sticky-toggle"));
        QTRY_VERIFY(!note->isVisible());
        QVERIFY(dispatch("sticky-toggle"));
        QTRY_VERIFY(note->isVisible());

        // Only the note in front shows its boxes (9.2.2): bring it forward first.
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(strip, 60, 7));
        QTRY_VERIFY(note->property("front").toBool() && item("sticky-close-" + name)->isVisible());
        // The close box asks first when the note has text; Return is Cancel, OK deletes it.
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(item("sticky-close-" + name)));
        auto *confirm = view.rootObject()->findChild<QObject *>("sticky-delete-dialog");
        QTRY_VERIFY(confirm && confirm->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!confirm->property("visible").toBool());
        QCOMPARE(notes->count(), 1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(item("sticky-close-" + name)));
        QTRY_VERIFY(confirm->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(confirm, "accept"));
        QTRY_COMPARE(notes->count(), 0);
        // An empty note goes without asking.
        QVERIFY(dispatch("sticky-new"));
        QTRY_COMPARE(notes->count(), 1);
        const auto empty = QString::number(notes->data(notes->index(0), StickyNotes::IdRole).toInt());
        // A new note comes forward, ready for typing, with its boxes.
        QTRY_VERIFY(item("sticky-close-" + empty) && item("sticky-close-" + empty)->isVisible());
        QTest::mouseClick(&view, Qt::LeftButton, {}, at(item("sticky-close-" + empty)));
        QTRY_COMPARE(notes->count(), 0);
        QVERIFY(!confirm->property("visible").toBool());
    }
    void controlStripAttachesToEdgesFoldsAndOpensPanelsFromModuleMenus()
    {
        QTemporaryDir root;
        // Personal assets live under XDG_DATA_HOME: keep this test's stand-in icon in its own folder.
        const auto previousData = qgetenv("XDG_DATA_HOME");
        const auto restoreData = qScopeGuard([&] { qputenv("XDG_DATA_HOME", previousData); });
        qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
        QQuickView view;
        view.resize(1120, 760);
        NativeManager manager(nullptr, &view, root.path(), root.path() + "/profile", {});
        QVERIFY(manager.personalAssets().startsWith(root.path()));
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings);
        // An icon imported from the user's own Mac OS 9 (here a stand-in) replaces our drawing.
        QVERIFY(QDir().mkpath(manager.personalAssets() + "/icons/control-strip"));
        {
            QImage icon(16, 16, QImage::Format_ARGB32);
            icon.fill(QColor("#336699"));
            QVERIFY(icon.save(manager.personalAssets() + "/icons/control-strip/energy.png"));
        }
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
        // Measured in the 9.2.2 reference VM: 24 px tall; the edge line, collapse box, two arrow
        // wells and end tab take 60 px, and each 30 px module 31 with its line.
        QCOMPARE(strip->height(), 24);
        const auto expandedWidth = strip->width();
        QCOMPARE(qRound(expandedWidth), 60 + 31 * int(strip->property("modules").toList().size()));
        QVERIFY(click("control-strip-collapse"));
        QTRY_VERIFY(settings->controlStripCollapsed());
        QCOMPARE(strip->width(), 18);
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
        const auto drag = [&](QPoint offset, Qt::KeyboardModifiers modifiers = {})
        {
            auto *tab = visualItem(view.contentItem(), "control-strip-tab");
            const auto point = tab->mapToScene(QPointF(8, 14)).toPoint();
            QTest::mousePress(&view, Qt::LeftButton, modifiers, point);
            QTest::mouseMove(&view, point + offset / 2, 30);
            QTest::mouseMove(&view, point + offset, 30);
            QTest::mouseRelease(&view, Qt::LeftButton, modifiers, point + offset, 30);
        };
        auto *outline = visualItem(view.contentItem(), "control-strip-outline");
        QVERIFY(outline && !outline->isVisible());
        // Option-drag moves it (verified in the 9.2.2 VM): only an outline follows the pointer,
        // attached to the edge on the pointer's side, and the strip moves on release.
        const auto old = strip->position();
        {
            auto *tab = visualItem(view.contentItem(), "control-strip-tab");
            const auto point = tab->mapToScene(QPointF(8, 14)).toPoint();
            // Leftwards: the strip's tab is near the middle of this 1120-pixel view, and the outline
            // attaches to the edge on the pointer's side.
            QTest::mousePress(&view, Qt::LeftButton, Qt::AltModifier, point);
            QTest::mouseMove(&view, point + QPoint(-40, -50), 30);
            QTest::mouseMove(&view, point + QPoint(-90, -100), 30);
            QTRY_VERIFY(outline->isVisible());
            QCOMPARE(strip->position(), old);
            QCOMPARE(qRound(outline->x()), -1);
            QCOMPARE(qRound(outline->y()), qRound(old.y() - 100 - 1));
            QCOMPARE(qRound(outline->width()), qRound(strip->width() + 2));
            QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                           "/artifacts/control-strip-move-outline.png"));
            QTest::mouseRelease(&view, Qt::LeftButton, Qt::AltModifier, point + QPoint(-90, -100), 30);
        }
        QTRY_COMPARE(strip->position(), QPointF(0, old.y() - 100));
        QVERIFY(!outline->isVisible());
        AppearanceSettings position(root.path() + "/profile");
        QCOMPARE(position.controlStripX(), 0);
        QCOMPARE(position.controlStripY(), qRound(strip->y()));
        // A plain drag that starts up or down moves it too (Option is not needed: Lily Hewitt found the
        // strip impossible to move with a plain drag, which in 9.2.2 only changes the length).
        const auto beforePlain = strip->position();
        drag(QPoint(4, -60));
        QTRY_COMPARE(strip->position(), QPointF(0, beforePlain.y() - 60));
        QCOMPARE(settings->controlStripModules(), 0); // The length is unchanged.
        drag(QPoint(-4, 60));
        QTRY_COMPARE(strip->position(), beforePlain);
        // It stops just below the menu bar.
        drag(QPoint(0, -2000), Qt::AltModifier);
        QTRY_COMPARE(strip->y(), 27);
        drag(QPoint(0, int(old.y()) - 100 - 27), Qt::AltModifier);
        QTRY_COMPARE(strip->y(), old.y() - 100);
        // With the pointer past the middle it attaches to the right edge, mirrored, and stays
        // there when folded.
        drag(QPoint(800, 0), Qt::AltModifier);
        QTRY_COMPARE(strip->x(), view.width() - strip->width());
        QCOMPARE(strip->y(), old.y() - 100);
        QVERIFY(sceneX("control-strip-tab") < sceneX("control-strip-sound"));
        QVERIFY(sceneX("control-strip-sound") < sceneX("control-strip-collapse"));
        AppearanceSettings right(root.path() + "/profile");
        QCOMPARE(right.controlStripX(), view.width() - 18);
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(settings->controlStripCollapsed());
        QCOMPARE(strip->x(), view.width() - 18);
        QVERIFY(click("control-strip-tab"));
        QTRY_VERIFY(!settings->controlStripCollapsed());
        QCOMPARE(strip->x(), view.width() - expandedWidth);
        // A plain drag changes the length instead, in whole modules, mirrored on the right edge,
        // and never the height.
        const int moduleCount = strip->property("modules").toList().size();
        QVERIFY(moduleCount >= 4);
        drag(QPoint(2 * 31, -60));
        QTRY_COMPARE(settings->controlStripModules(), moduleCount - 2);
        QCOMPARE(strip->width(), expandedWidth - 2 * 31);
        QCOMPARE(strip->x(), view.width() - strip->width());
        QCOMPARE(strip->y(), old.y() - 100);
        QVERIFY(visualItem(view.contentItem(), "control-strip-scroll-forward")->property("active").toBool());
        // The length stays when the strip moves.
        drag(QPoint(-800, 0), Qt::AltModifier);
        QTRY_COMPARE(strip->x(), 0);
        QCOMPARE(strip->width(), expandedWidth - 2 * 31);
        {
            // While resizing, the strip keeps its length and the outline shows the new one.
            auto *tab = visualItem(view.contentItem(), "control-strip-tab");
            const auto point = tab->mapToScene(QPointF(8, 14)).toPoint();
            QTest::mousePress(&view, Qt::LeftButton, {}, point);
            QTest::mouseMove(&view, point + QPoint(-20, 0), 30);
            QTest::mouseMove(&view, point + QPoint(-36, 0), 30);
            QTRY_VERIFY(outline->isVisible());
            QCOMPARE(qRound(outline->width()), qRound(strip->width() - 36 + 2));
            QCOMPARE(strip->width(), expandedWidth - 2 * 31);
            QTest::mouseRelease(&view, Qt::LeftButton, {}, point + QPoint(-36, 0), 30);
        }
        QTRY_COMPARE(settings->controlStripModules(), moduleCount - 3);
        // Dragged back to the edge it collapses; dragged out again it opens to that length.
        drag(QPoint(-2000, 0));
        QTRY_VERIFY(settings->controlStripCollapsed());
        QCOMPARE(strip->width(), 18);
        drag(QPoint(114, 40)); // 18 + 114: the fixed parts and two modules, nearly.
        QTRY_VERIFY(!settings->controlStripCollapsed());
        QCOMPARE(settings->controlStripModules(), 2);
        QCOMPARE(strip->width(), 60 + 2 * 31);
        QCOMPARE(strip->x(), 0);
        // Long enough for every module, it shows them all again.
        drag(QPoint(2000, 0));
        QTRY_COMPARE(settings->controlStripModules(), 0);
        QCOMPARE(strip->width(), expandedWidth);
        AppearanceSettings length(root.path() + "/profile");
        QCOMPARE(length.controlStripModules(), 0);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/control-strip.png"));
        // Modules open pop-up menus above the strip; the first item opens the owning panel.
        QVERIFY(click("control-strip-appearance"));
        auto *menu = visualItem(view.contentItem(), "control-strip-menu");
        QVERIFY(menu);
        QTRY_VERIFY(menu->isVisible());
        // Its bottom edge overlaps the strip's top by 3 px, its left edge 1 px left of the module.
        QCOMPARE(qRound(menu->y() + menu->height()), qRound(strip->y()) + 3);
        QCOMPARE(qRound(menu->x()), qRound(visualItem(view.contentItem(), "control-strip-appearance")
                                               ->mapToScene(QPointF(0, 0)).x()) - 1);
        QCOMPARE(strip->property("openModule").toString(), QString("appearance"));
        QVERIFY(choose("desktop-command-appearance"));
        auto *panel = view.rootObject()->findChild<QObject *>("appearance-dialog");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(!manager.shellModal()); // A panel of the modeless Control Panels window.
        // The panel opens on its Appearance tab; WindowShade speed lives under Options.
        QCOMPARE(panel->property("tab").toInt(), 0);
        QVERIFY(choose("appearance-tab-options"));
        QTRY_COMPARE(panel->property("tab").toInt(), 4);
        QVERIFY(QMetaObject::invokeMethod(panel, "setShadeDuration", Q_ARG(QVariant, 600)));
        QVERIFY(choose("shade-preview-button"));
        auto *preview = view.rootObject()->findChild<QQuickItem *>("shade-preview");
        QVERIFY(preview);
        QTRY_VERIFY(preview->property("animating").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/appearance.png"));
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        QCOMPARE(settings->shadeDuration(), 600); // Applied when chosen.
        QVERIFY(QMetaObject::invokeMethod(panel, "setShadeDuration", Q_ARG(QVariant, 240)));
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
        QVERIFY(choose("appearance-tab-options"));
        QVERIFY(choose("show-control-strip"));
        QTRY_VERIFY(!strip->isVisible());
        QVERIFY(!settings->controlStripVisible());
        QCOMPARE(settings->shadeDuration(), 240);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        // The menu provides a way back when the strip is hidden: Apple menu >
        // Control Panels, which also shows the strip's state with a check mark.
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(25, 13));
        QTRY_COMPARE(view.rootObject()->property("openMenu").toInt(), 0);
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
        QVERIFY(QMetaObject::invokeMethod(panel, "setStripVisible", Q_ARG(QVariant, true)));
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
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        // Energy Settings, as 9.2.2's: its control panel, a line, Sleep Now (only where this
        // session may sleep the computer; never chosen here).
        {
            auto *energyModule = visualItem(view.contentItem(), "control-strip-energy");
            QVERIFY(energyModule);
            auto *imported = visualItem(energyModule, "control-strip-personal-icon");
            QTRY_VERIFY(imported && imported->isVisible());
            auto *sound = visualItem(view.contentItem(), "control-strip-sound");
            QVERIFY(!visualItem(sound, "control-strip-personal-icon")->isVisible());
        }
        QVERIFY(click("control-strip-energy"));
        QTRY_VERIFY(menu->isVisible());
        auto *sleepNow = visualItem(view.contentItem(), "desktop-command-sleep-now");
        QTRY_VERIFY(sleepNow && sleepNow->isVisible());
        QCOMPARE(sleepNow->property("modelData").toMap().value("enabled").toBool(), manager.powerAvailable());
        QVERIFY(choose("desktop-command-energy"));
        auto *energy = view.rootObject()->findChild<QObject *>("energy-panel");
        QVERIFY(energy);
        QTRY_VERIFY(energy->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!energy->property("opened").toBool());
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
        QVERIFY(!manager.shellModal()); // A panel of the modeless Control Panels window.
        QCOMPARE(requested.count(), 1);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(manager.rowCount(), 0); // Internal panel, no application process.
        QCOMPARE(panel->property("service").value<QObject *>(), service);
        // Asking again brings the same window forward: still no process, nothing new opened.
        manager.launchApplication("org.frankenstein.Bluetooth.desktop");
        QTRY_COMPARE(requested.count(), 2);
        QCOMPARE(manager.rowCount(), 0);
        QVERIFY(panel->property("opened").toBool());
        QTRY_VERIFY(visualItem(view.contentItem(), "control-panels-window")->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_Escape);
        QTRY_VERIFY(!panel->property("opened").toBool());
        const auto document = fixture.filePath("file.txt");
        QFile file(document);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("text"); file.close();
        manager.launchApplication("org.frankenstein.Bluetooth.desktop", document);
        QCOMPARE(requested.count(), 2);
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
        // Mac OS 9's focus ring around the field with the keyboard, as in application text fields.
        auto *frame = field->property("background").value<QQuickItem *>();
        QVERIFY(frame);
        QTRY_COMPARE(frame->property("color").value<QColor>(), QColor("#666699"));
        commandKey(&view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&view, "Context folder"));
        QTest::keyClick(&view, Qt::Key_Return);
        QTRY_VERIFY(!manager.shellModal() && !items->busy());
        QTRY_VERIFY(!frame->property("focused").toBool());
        QCOMPARE(frame->property("color").value<QColor>().alpha(), 0);
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
    // Restart Desktop (Lily Hewitt, 2026-11-12): the desktop notes what to open again, ends with
    // RestartDesktopExit, and the next desktop opens it once, if the note is fresh.
    void restartDesktopNotesWhatToReopen()
    {
        QTemporaryDir fixture;
        QQuickView view;
        const auto profile = fixture.path() + "/profile";
        NativeManager manager(nullptr, &view, fixture.path(), profile, {});
        QSignalSpy ended(&manager, &NativeManager::sessionEnded);
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        // Under a session supervisor from before update 48, a restart would be a Log Out: refused.
        qunsetenv("PLATINUM_DESKTOP_RESTART");
        manager.restartDesktop({"calculator"});
        QCOMPARE(ended.count(), 0);
        QCOMPARE(errors.count(), 1);
        QVERIFY(!QFileInfo::exists(profile + "/reopen-after-restart.json"));
        qputenv("PLATINUM_DESKTOP_RESTART", "75");
        const auto unset = qScopeGuard([] { qunsetenv("PLATINUM_DESKTOP_RESTART"); });
        manager.restartDesktop({"calculator", "note-pad", "../bad"});
        QCOMPARE(ended.count(), 1);
        QCOMPARE(ended.at(0).at(0).toInt(), NativeManager::RestartDesktopExit);
        QFile note(profile + "/reopen-after-restart.json");
        QVERIFY(note.open(QIODevice::ReadOnly));
        const auto saved = QJsonDocument::fromJson(note.readAll()).object();
        note.close();
        QCOMPARE(saved.value("shell").toArray(), (QJsonArray{"calculator", "note-pad"}));
        QCOMPARE(saved.value("applications").toArray(), QJsonArray{});

        NativeManager next(nullptr, &view, fixture.path(), profile, {});
        QSignalSpy reopen(&next, &NativeManager::reopenShellWindows);
        next.reopenAfterRestart();
        QCOMPARE(reopen.count(), 1);
        QCOMPARE(reopen.at(0).at(0).toStringList(), (QStringList{"calculator", "note-pad"}));
        QVERIFY(!QFileInfo::exists(note.fileName())); // once only
        next.reopenAfterRestart();
        QCOMPARE(reopen.count(), 1);

        // A note left by a restart that never finished is old news at the next login.
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write(QJsonDocument(QJsonObject{{"shell", QJsonArray{"calculator"}},
                                             {"written", QDateTime::currentSecsSinceEpoch() - 3600}}).toJson());
        note.close();
        next.reopenAfterRestart();
        QCOMPARE(reopen.count(), 1);
        QVERIFY(!QFileInfo::exists(note.fileName()));
        // Log Out still ends with 0.
        NativeManager last(nullptr, &view, fixture.path(), profile, {});
        QSignalSpy loggedOut(&last, &NativeManager::sessionEnded);
        last.quitSession();
        QCOMPARE(loggedOut.count(), 1);
        QCOMPARE(loggedOut.at(0).at(0).toInt(), 0);
        QVERIFY(!QFileInfo::exists(note.fileName()));
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
        const auto clearView = qScopeGuard([&] { view.setSource({}); });
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *root = view.rootObject();
        QVERIFY(root);
        QCOMPARE(root->property("openMenu").toInt(), -1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(25, 13));
        QCOMPARE(root->property("openMenu").toInt(), 0);
        auto *file = visualItem(view.contentItem(), "bar-entry-1");
        QVERIFY(file);
        file->parentItem()->ensurePolished();
        const auto filePoint = file->mapToScene(QPointF(file->width()/2, 8)).toPoint();
        QTest::mouseMove(&view, filePoint);
        QTRY_COMPARE(root->property("openMenu").toInt(), 1);
        QTest::mouseMove(&view, filePoint + QPoint(1, 0));
        QCOMPARE(root->property("openMenu").toInt(), 1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(500, 500));
        QCOMPARE(root->property("openMenu").toInt(), -1);
        QTest::mouseClick(&view, Qt::LeftButton, {}, filePoint);
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
        // 9.2.2's etched strip: solid lines, not dots alone (Lily Hewitt, 2026-10-22).
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
        // Dragging down past the open menu tears it off as the Application Switcher (9.2.2).
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(600, 400)); // Close the menu first.
        QTRY_COMPARE(root->property("openMenu").toInt(), -1);
        auto *switcher = visualItem(view.contentItem(), "application-switcher");
        auto *outline = visualItem(view.contentItem(), "application-switcher-outline");
        QVERIFY(switcher && outline && !switcher->isVisible());
        const auto start = title->mapToScene(QPointF(title->width() / 2, 13)).toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, start);
        QTRY_COMPARE(root->property("openMenu").toInt(), root->property("barEntries").toList().size());
        QTest::mouseMove(&view, start + QPoint(-20, 60), 30);
        QTest::mouseMove(&view, start + QPoint(-200, 250), 30);
        QTRY_VERIFY(outline->isVisible());
        QTest::mouseRelease(&view, Qt::LeftButton, {}, start + QPoint(-200, 250), 30);
        QTRY_VERIFY(switcher->isVisible());
        QVERIFY(!outline->isVisible());
        QTRY_COMPARE(root->property("openMenu").toInt(), -1);
        QCOMPARE(qRound(switcher->y()), start.y() + 250);
        auto *settings = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(settings->switcher().value("visible").toBool());
        AppearanceSettings saved(fixture.path() + "/profile");
        QCOMPARE(saved.switcher().value("y").toInt(), start.y() + 250);
        // A row for each running application; the one in front pressed in.
        auto *finder = visualItem(view.contentItem(), "application-switcher-Finder");
        QVERIFY(finder && finder->property("down").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath() +
                                       "/artifacts/application-switcher.png"));
        auto *close = visualItem(view.contentItem(), "application-switcher-close");
        QTest::mouseClick(&view, Qt::LeftButton, {}, close->mapToScene(QPointF(5, 5)).toPoint());
        QTRY_VERIFY(!switcher->isVisible());
        QVERIFY(!settings->switcher().value("visible").toBool());
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
        // Command-Shift-Delete asks the same question.
        QTest::keyClick(&view, Qt::Key_Backspace, Qt::MetaModifier | Qt::ShiftModifier);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(&view, Qt::Key_Return);
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
    void computerMemorySnapshotKeepsUnknownsAndDeduplicatesProcesses()
    {
        QTemporaryDir root;
        const auto write=[&](const QString &name,const QByteArray &data){
            const auto path=root.filePath(name);QDir().mkpath(QFileInfo(path).absolutePath());
            QFile file(path);return file.open(QIODevice::WriteOnly) && file.write(data)==data.size();
        };
        QVERIFY(write("meminfo","MemTotal: 8192 kB\nMemAvailable: 4096 kB\nSwapTotal: 2048 kB\nSwapFree: 1024 kB\n"));
        QVERIFY(write("10/status","VmRSS: 256 kB\n"));
        QVERIFY(write("11/status","VmRSS: 512 kB\n"));
        const QList<ComputerApplication> apps{{1,"First",{10,10}},{2,"Second",{11}},{3,"Exited",{12}},{4,"No PID",{}}};
        auto state=computerInformation(apps,root.path());
        QCOMPARE(state.value("ramBytes").toLongLong(),8192*1024LL);
        QCOMPARE(state.value("swapBytes").toLongLong(),2048*1024LL);
        QCOMPARE(state.value("largestUnusedBlock").toString(),QString("Not reported"));
        const auto rows=state.value("applications").toList();
        QCOMPARE(rows[0].toMap().value("rssBytes").toLongLong(),256*1024LL);
        QCOMPARE(rows[0].toMap().value("fraction").toDouble(),0.5);
        QCOMPARE(rows[1].toMap().value("fraction").toDouble(),1.0);
        QVERIFY(!rows[2].toMap().value("known").toBool());
        QVERIFY(!rows[3].toMap().value("known").toBool());
        // A later snapshot reads current values, with no cached PID/status data.
        QVERIFY(write("11/status","VmRSS: 128 kB\n"));
        state=computerInformation(apps,root.path());
        QCOMPARE(state.value("applications").toList()[1].toMap().value("rssBytes").toLongLong(),128*1024LL);
        for(const auto &bad:{QByteArray("MemTotal: -1 kB\n"),QByteArray("MemTotal: 18446744073709551615 kB\n"),
                            QByteArray("MemTotal: 1 kB\nMemTotal: 2 kB\n"),QByteArray("MemTotal: 8192 bytes\n"),QByteArray(65537,'x')}){
            QVERIFY(write("meminfo",bad));
            const auto invalid=computerInformation(apps,root.path());
            QVERIFY(!invalid.value("ramBytes").isValid());
            QCOMPARE(invalid.value("ram").toString(),QString("Unavailable"));
        }
        QVERIFY(write("meminfo","MemTotal: 8192 kB\nMemAvailable: 9999 kB\nSwapTotal: 0 kB\nSwapFree: 0 kB\n"));
        state=computerInformation(apps,root.path());
        QCOMPARE(state.value("available").toString(),QString("Unavailable"));
        QCOMPARE(state.value("swap").toString(),QString("Off"));
    }
    void aboutComputerIsOneLiveModelessWindowAndStopsSamplingWhenClosed()
    {
        QTemporaryDir root;
        QQuickView view;view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status()!=QQuickView::Error,qPrintable(view.errors().isEmpty()?QString():view.errors()[0].toString()));
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto open=[&]{return QMetaObject::invokeMethod(view.rootObject(),"dispatchShellCommand",
            Q_ARG(QVariant,QVariant("about")),Q_ARG(QVariant,QVariant(0)));};
        QVERIFY(open());
        auto *window=visualItem(view.contentItem(),"about-computer-window");
        QVERIFY(window);QTRY_VERIFY(window->isVisible());
        QVERIFY(!manager.shellModal());
        const auto oldPosition=window->position();
        const auto title=window->mapToScene(QPointF(60,12)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},title);
        QTest::mouseMove(&view,title+QPoint(30,15),30);
        QTest::mouseMove(&view,title+QPoint(60,30),30);
        QTest::mouseRelease(&view,Qt::LeftButton,{},title+QPoint(60,30),30);
        QVERIFY(window->x()>oldPosition.x() && window->y()>oldPosition.y());
        const auto first=window->property("samples").toInt();QVERIFY(first>0);
        QVERIFY(open());
        QCOMPARE(visualItem(view.contentItem(),"about-computer-window"),window);
        QTRY_VERIFY_WITH_TIMEOUT(window->property("samples").toInt()>first,1600);
        const auto property=window->property("snapshot");
        const auto snapshot=property.metaType()==QMetaType::fromType<QJSValue>()
            ? property.value<QJSValue>().toVariant().toMap() : property.toMap();
        QVERIFY(!snapshot.value("system").toString().isEmpty());
        QVERIFY(!snapshot.value("applications").toList().isEmpty());
        // The system's own row (the desktop with its Finder) comes last, named for the system.
        const auto last=snapshot.value("applications").toList().last().toMap();
        QCOMPARE(last.value("name").toString(),QString("Platinum Reality"));
        QVERIFY(last.value("system").toBool());
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/about-computer.png"));
        // 9.2's window controls: the collapse box rolls it up and back, the size box resizes it
        // (never below its minimum), the zoom box switches between that size and the zoomed one.
        const auto clickItem=[&](const QString &name){
            auto *box=visualItem(view.contentItem(),name);
            if(!box || !box->isVisible())return false;
            QTest::mouseClick(&view,Qt::LeftButton,{},box->mapToScene(QPointF(box->width()/2,box->height()/2)).toPoint());
            return true;
        };
        const auto openHeight=window->height();
        QVERIFY(clickItem("about-computer-window-collapse"));
        QTRY_COMPARE(window->height(),24.0);
        QVERIFY(!visualItem(view.contentItem(),"about-applications")->isVisible());
        QVERIFY(clickItem("about-computer-window-collapse"));
        QTRY_COMPARE(window->height(),openHeight);
        auto *size=visualItem(view.contentItem(),"about-computer-window-size");
        QVERIFY(size && size->isVisible());
        const auto grip=size->mapToScene(QPointF(7,7)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},grip);
        QTest::mouseMove(&view,grip+QPoint(40,60),30);
        QTest::mouseMove(&view,grip+QPoint(80,120),30);
        QTest::mouseRelease(&view,Qt::LeftButton,{},grip+QPoint(80,120),30);
        QTRY_COMPARE(window->width(),560.0+80);
        QCOMPARE(window->height(),openHeight+120);
        const auto corner=size->mapToScene(QPointF(7,7)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},corner);
        QTest::mouseMove(&view,corner-QPoint(200,200),30);
        QTest::mouseMove(&view,corner-QPoint(400,300),30);
        QTest::mouseRelease(&view,Qt::LeftButton,{},corner-QPoint(400,300),30);
        QTRY_COMPARE(window->width(),470.0);
        QCOMPARE(window->height(),260.0);
        QVERIFY(clickItem("about-computer-window-zoom"));
        QTRY_VERIFY(window->property("zoomed").toBool());
        QVERIFY(window->height()>260.0);
        QVERIFY(clickItem("about-computer-window-zoom"));
        QTRY_COMPARE(window->height(),260.0);
        commandKey(&view,Qt::Key_W,17);
        QTRY_VERIFY(!window->isVisible());
        const auto stopped=window->property("samples").toInt();
        QTest::qWait(1100);QCOMPARE(window->property("samples").toInt(),stopped);
        QVERIFY(open());
        QTest::keyClick(&view,Qt::Key_Escape);QTRY_VERIFY(!window->isVisible());
    }
    void accessoryOutlineDefersMovementAndCancelsWithoutClosing()
    {
        QTemporaryDir root; QQuickView view; view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(),"dispatchShellCommand",
            Q_ARG(QVariant,QVariant("about")),Q_ARG(QVariant,QVariant(0))));
        auto *window=visualItem(view.contentItem(),"about-computer-window"); QVERIFY(window);
        QTRY_VERIFY(window->isVisible() && window->hasActiveFocus());
        const auto origin=window->position();
        const auto title=window->mapToScene(QPointF(60,12)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},title);
        QTRY_VERIFY(manager.windowMoveActive());
        QTest::mouseMove(&view,title+QPoint(60,30),30);
        QCOMPARE(window->position(),origin);
        QCOMPARE(manager.windowMoveRect().topLeft(),origin.toPoint()+QPoint(60,30));
        auto *outline=visualItem(view.contentItem(),"window-move-outline"); QVERIFY(outline);
        QTRY_VERIFY(outline->isVisible());
        QTest::mouseRelease(&view,Qt::LeftButton,{},title+QPoint(60,30));
        QTRY_VERIFY(!manager.windowMoveActive());
        QCOMPARE(window->position(),origin+QPointF(60,30));
        const auto committed=window->position();
        const auto second=window->mapToScene(QPointF(60,12)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},second);
        QTest::mouseMove(&view,second+QPoint(30,20),30);
        QTRY_VERIFY(manager.windowMoveActive());
        QTest::keyClick(&view,Qt::Key_Escape);
        QTRY_VERIFY(!manager.windowMoveActive());
        QVERIFY(window->isVisible()); // Escape cancels the move before accessory close.
        QTest::mouseRelease(&view,Qt::LeftButton,{},second+QPoint(30,20));
        QCOMPARE(window->position(),committed);
        QTest::mousePress(&view,Qt::LeftButton,{},second);
        QTRY_VERIFY(manager.windowMoveActive());
        window->setWidth(window->width()+1);
        QTRY_VERIFY(!manager.windowMoveActive());
        QTest::mouseRelease(&view,Qt::LeftButton,{},second);
        QCOMPARE(window->position(),committed);
        QTest::mousePress(&view,Qt::LeftButton,{},second);
        QTRY_VERIFY(manager.windowMoveActive());
        window->setVisible(false);
        QTRY_VERIFY(!manager.windowMoveActive());
        QTest::mouseRelease(&view,Qt::LeftButton,{},second);
        QCOMPARE(window->position(),committed);
    }
    void switcherOutlineCommitsSettingsOnlyOnRelease()
    {
        QTemporaryDir root; QQuickView view; view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *settings=qobject_cast<AppearanceSettings*>(manager.appearance()); QVERIFY(settings);
        QVERIFY(settings->setSwitcher(true,80,120));
        auto *palette=visualItem(view.contentItem(),"application-switcher"); QVERIFY(palette);
        QTRY_VERIFY(palette->isVisible());
        const auto origin=palette->position();
        const auto title=palette->mapToScene(QPointF(60,8)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},title);
        QTRY_VERIFY(manager.windowMoveActive());
        QTest::mouseMove(&view,title+QPoint(60,30),30);
        QCOMPARE(palette->position(),origin);
        QCOMPARE(settings->switcher().value("x").toInt(),80);
        QTest::mouseRelease(&view,Qt::LeftButton,{},title+QPoint(60,30));
        QTRY_VERIFY(!manager.windowMoveActive());
        QCOMPARE(palette->position(),origin+QPointF(60,30));
        QCOMPARE(settings->switcher().value("x").toInt(),140);
        QCOMPARE(settings->switcher().value("y").toInt(),150);
        const auto committed=palette->position();
        const auto second=palette->mapToScene(QPointF(60,8)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},second);
        QTest::mouseMove(&view,second+QPoint(40,20),30);
        QTRY_VERIFY(manager.windowMoveActive());
        QTest::keyClick(&view,Qt::Key_Escape);
        QTRY_VERIFY(!manager.windowMoveActive());
        QTest::mouseRelease(&view,Qt::LeftButton,{},second+QPoint(40,20));
        QCOMPARE(palette->position(),committed);
        QCOMPARE(settings->switcher().value("x").toInt(),140);
        // Restored bindings still respond to an independent saved placement change.
        QVERIFY(settings->setSwitcher(true,180,190));
        QTRY_COMPARE(palette->position(),origin+QPointF(100,70));
    }
    void separateAccessoryFramesUseOutlineDragging_data()
    {
        QTest::addColumn<QString>("componentName");
        for (const auto &name : {"InfoWindow", "HelpWindow", "ControlPanelsWindow", "ClipboardWindow"})
            QTest::newRow(name) << QString(name);
    }
    void separateAccessoryFramesUseOutlineDragging()
    {
        QFETCH(QString,componentName);
        QTemporaryDir root; QQuickView view; view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        QQmlComponent component(view.engine(), QUrl("qrc:/desktop/src/desktop/"+componentName+".qml"));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QVariantMap properties;
        if (componentName=="InfoWindow") properties["details"]=QVariantMap{
            {"key","owned"},{"name","Owned"},{"kind","file"},{"kindLabel","Document"},
            {"size","12 bytes"},{"where","/tmp"},{"created","Unavailable"},{"modified","Unavailable"}};
        if (componentName=="ClipboardWindow") properties["clipboard"]=QVariantMap{{"kind","text"},{"text","owned"}};
        std::unique_ptr<QObject> owned(component.createWithInitialProperties(properties));
        auto *window=qobject_cast<QQuickItem*>(owned.get()); QVERIFY(window);
        window->setParentItem(view.contentItem()); window->setPosition({80,80}); window->setVisible(true);
        view.show(); QVERIFY(QTest::qWaitForWindowExposed(&view)); window->forceActiveFocus();
        const auto origin=window->position();
        const auto title=window->mapToScene(QPointF(60,12)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},title);
        QTRY_VERIFY(manager.windowMoveActive());
        QTest::mouseMove(&view,title+QPoint(40,20),30);
        QCOMPARE(window->position(),origin);
        QTest::mouseRelease(&view,Qt::LeftButton,{},title+QPoint(40,20));
        QTRY_VERIFY(!manager.windowMoveActive());
        QCOMPARE(window->position(),origin+QPointF(40,20));
        const auto committed=window->position();
        const auto second=window->mapToScene(QPointF(60,12)).toPoint();
        QTest::mousePress(&view,Qt::LeftButton,{},second);
        QTRY_VERIFY(manager.windowMoveActive());
        QQuickItem other(view.contentItem());
        other.forceActiveFocus();
        QTRY_VERIFY(other.hasActiveFocus());
        QTRY_VERIFY(!window->hasActiveFocus());
        QTRY_VERIFY(!manager.windowMoveActive());
        QTest::mouseRelease(&view,Qt::LeftButton,{},second+QPoint(20,10));
        QCOMPARE(window->position(),committed);
    }
    void classicControlPanelChangesTheRealSettings()
    {
        QTemporaryDir root;
        QQuickView view;view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY2(view.status()!=QQuickView::Error,qPrintable(view.errors().isEmpty()?QString():view.errors()[0].toString()));
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *appearance=qobject_cast<AppearanceSettings *>(manager.appearance());
        auto *input=qobject_cast<InputSettings *>(manager.input());
        QVERIFY(appearance && input);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(),"dispatchShellCommand",
            Q_ARG(QVariant,QVariant("control-panel")),Q_ARG(QVariant,QVariant(0))));
        auto *window=visualItem(view.contentItem(),"classic-control-panel");
        QVERIFY(window);QTRY_VERIFY(window->isVisible() && window->hasActiveFocus());
        QVERIFY(!manager.shellModal());
        const auto item=[&](const QString &name){view.grabWindow();return visualItem(view.contentItem(),name);};
        const auto click=[&](const QString &name,QPointF at=QPointF(-1,-1)){
            auto *target=item(name);
            if(!target || !target->isVisible())return false;
            const auto point=at.x()<0?QPointF(target->width()/2,9):at;
            QTest::mouseClick(&view,Qt::LeftButton,{},target->mapToScene(point).toPoint());
            return true;
        };
        // Menu blinking and the insertion point: the same settings as General Controls.
        QVERIFY(click("classic-menu-blink-0"));
        QTRY_COMPARE(appearance->general().value("menuBlinks").toInt(),0);
        QVERIFY(click("classic-caret-2"));
        QTRY_COMPARE(appearance->general().value("caretBlink").toInt(),2);
        // The clock: 24 hours, then off in the menu bar, and on again.
        QVERIFY(click("classic-24hr"));
        QTRY_VERIFY(appearance->clock().value("hours24").toBool());
        QVERIFY(item("classic-time")->property("text").toString().size()==8); // HH:mm:ss
        QVERIFY(click("classic-clock-off"));
        QTRY_VERIFY(!item("menu-bar-clock")->isVisible());
        QVERIFY(click("classic-clock-on"));
        QTRY_VERIFY(item("menu-bar-clock")->isVisible());
        // A drawn pattern goes on the desktop when its preview is clicked: tiled, from the profile.
        QCOMPARE(appearance->desktopPattern(),QString());
        QVERIFY(click("classic-pattern-next"));
        QVERIFY(click("classic-pattern-editor",QPointF(1+5,1+5))); // The top-left dot.
        QVERIFY(click("classic-pattern-preview",QPointF(40,30)));
        QTRY_COMPARE(appearance->desktopPattern(),QString("0822882288228822"));
        QCOMPARE(appearance->wallpaperLayout(),QString("tile"));
        QVERIFY(QFile::exists(root.filePath("profile/patterns/0822882288228822.png")));
        auto *wallpaper=item("desktop-wallpaper");
        QVERIFY(wallpaper);
        QTRY_VERIFY(wallpaper->property("source").toUrl().toLocalFile().endsWith("patterns/0822882288228822.png"));
        QTRY_COMPARE(wallpaper->property("status").toInt(),1); // Image.Ready
        QCOMPARE(wallpaper->property("fillMode").toInt(),3); // Image.Tile
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/classic-control-panel.png"));
        QVERIFY(click("classic-pattern-next"));
        QVERIFY(click("classic-pattern-preview",QPointF(40,30)));
        QTRY_COMPARE(appearance->desktopPattern(),QString("77dd77dd77dd77dd"));
        QVERIFY(!QFile::exists(root.filePath("profile/patterns/0822882288228822.png"))); // The old one is gone.
        AppearanceSettings reread(root.filePath("profile"));
        QCOMPARE(reread.desktopPattern(),QString("77dd77dd77dd77dd"));
        QVERIFY(!reread.setDesktopPattern("not a pattern")); // Refused on a second reader: no alert here.
        // Keyboard and Mouse: rows of buttons for the same values as their panels.
        QVERIFY(click("classic-control-panel-keyboard",QPointF(60,40)));
        QTRY_VERIFY(item("classic-keyboard")->isVisible());
        QVERIFY(click("classic-repeat-rate-4"));
        QTRY_COMPARE(input->keyboard().value("repeatRate").toInt(),40);
        QVERIFY(click("classic-repeat-delay-0"));
        QTRY_COMPARE(input->keyboard().value("repeatDelay").toInt(),1200);
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/classic-control-panel-keyboard.png"));
        QVERIFY(click("classic-control-panel-mouse",QPointF(60,40)));
        QTRY_VERIFY(item("classic-mouse")->isVisible());
        QVERIFY(click("classic-tracking-0"));
        QTRY_COMPARE(input->pointer().value("speed").toDouble(),-0.75);
        // The others open their Control Panels panel.
        QVERIFY(click("classic-control-panel-monitors",QPointF(60,40)));
        QVERIFY(click("classic-open-panel"));
        auto *monitors=view.rootObject()->findChild<QObject *>("monitors-panel");
        QTRY_VERIFY(monitors && monitors->property("opened").toBool());
        // It stays open beside the other windows; Escape doesn't close it, its close box does.
        QVERIFY(window->isVisible());
        window->forceActiveFocus();
        QTest::keyClick(&view,Qt::Key_Escape);
        QTest::qWait(100);
        QVERIFY(window->isVisible());
        QVERIFY(click("classic-control-panel-close",QPointF(7,7)));
        QTRY_VERIFY(!window->isVisible());
    }
    void calculatorUsesImmediateArithmeticAndBoundsInput_data()
    {
        QTest::addColumn<QStringList>("keys");
        QTest::addColumn<QString>("answer");
        QTest::newRow("addition")<<QStringList{"2","+","3","="}<<QString("5");
        QTest::newRow("repeat-equals")<<QStringList{"2","+","3","=","="}<<QString("8");
        QTest::newRow("immediate-chain")<<QStringList{"2","+","3","*","4","="}<<QString("20");
        QTest::newRow("replace-operator")<<QStringList{"8","+","*","2","="}<<QString("16");
        QTest::newRow("decimal")<<QStringList{".","1","+",".","2","="}<<QString("0.3");
        QTest::newRow("divide")<<QStringList{"9","/","2","="}<<QString("4.5");
        QTest::newRow("zero-divisor")<<QStringList{"7","/","0","="}<<QString("Error");
        QTest::newRow("recover-error")<<QStringList{"7","/","0","=","8"}<<QString("8");
        QTest::newRow("sign")<<QStringList{"5","sign","*","2","="}<<QString("-10");
        QTest::newRow("percent-add")<<QStringList{"2","0","0","+","1","0","%","="}<<QString("220");
        QTest::newRow("percent-multiply")<<QStringList{"2","0","0","*","1","0","%","="}<<QString("20");
        QTest::newRow("backspace")<<QStringList{"1","2","3","backspace"}<<QString("12");
        QTest::newRow("implied-operand")<<QStringList{"4","+","="}<<QString("8");
        QTest::newRow("new-entry")<<QStringList{"2","+","3","=","9"}<<QString("9");
        QTest::newRow("ignored-text")<<QStringList{"2","1+2","process.exit()"}<<QString("2");
        QStringList bounded;for(int i=0;i<40;++i)bounded<<QString("9");
        QTest::newRow("input-limit")<<bounded<<QString("999999999999999");
        QStringList overflow=bounded;overflow<<QString("*");overflow+=bounded;overflow<<QString("=");
        for(int i=0;i<30;++i)overflow<<QString("=");
        QTest::newRow("overflow")<<overflow<<QString("Error");
    }
    void calculatorUsesImmediateArithmeticAndBoundsInput()
    {
        QFETCH(QStringList,keys);QFETCH(QString,answer);
        QQuickView view;
        view.setSource(QUrl("qrc:/desktop/src/desktop/CalculatorWindow.qml"));
        QVERIFY2(view.status()!=QQuickView::Error,qPrintable(view.errors().isEmpty()?QString():view.errors()[0].toString()));
        auto *calculator=view.rootObject();QVERIFY(calculator);
        for(const auto &key:keys)
            QVERIFY(QMetaObject::invokeMethod(calculator,"press",Q_ARG(QVariant,key)));
        QCOMPARE(calculator->property("display").toString(),answer);
    }
    void calculatorOpensOnceTakesKeyboardAndCopiesItsResult()
    {
        QTemporaryDir root;
        QQuickView view;view.resize(1120,760);
        NativeManager manager(nullptr,&view,root.path(),root.filePath("profile"),{});
        auto *items=qobject_cast<DesktopItems *>(manager.desktopItems());QVERIFY(items);
        QFile document(items->directory()+"/Selected document.txt");
        QVERIFY(document.open(QIODevice::WriteOnly));document.write("Leave this document in place.");document.close();
        items->refresh();
        for(int row=0;row<items->rowCount();++row)
            if(items->data(items->index(row),DesktopItems::NameRole)=="Selected document.txt")
                items->select(items->data(items->index(row),DesktopItems::KeyRole).toString());
        QCOMPARE(items->selectionCount(),1);
        QSignalSpy operations(items,&DesktopItems::operationFinished);
        view.rootContext()->setContextProperty("manager",&manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status()!=QQuickView::Error);
        const auto clearView=qScopeGuard([&]{view.setSource({});});
        view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto dispatch=[&](const QString &action){return QMetaObject::invokeMethod(view.rootObject(),"dispatchShellCommand",
            Q_ARG(QVariant,action),Q_ARG(QVariant,QVariant(0)));};
        QVERIFY(dispatch("calculator"));
        auto *calculator=visualItem(view.contentItem(),"calculator-window");QVERIFY(calculator);
        QSignalSpy closed(calculator,SIGNAL(closed()));
        QTRY_VERIFY(calculator->isVisible() && calculator->hasActiveFocus());
        QVERIFY(!manager.shellModal());
        const auto click=[&](const QString &name){
            auto *item=visualItem(calculator,name);if(!item)return false;
            QTest::mouseClick(&view,Qt::LeftButton,{},item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;
        };
        QVERIFY(click("calculator-key-7"));QTest::keyClick(&view,Qt::Key_Plus);
        QTest::keyClick(&view,Qt::Key_5);QTest::keyClick(&view,Qt::Key_Return);
        QCOMPARE(calculator->property("display").toString(),QString("12"));
        QSignalSpy copied(calculator,SIGNAL(copyRequested(QString)));
        commandKey(&view,Qt::Key_C,46);
        QCOMPARE(copied.count(),1);QCOMPARE(copied[0][0].toString(),QString("12"));
        // Edit ▸ Copy (the desktop's Edit menu acts on the Calculator in front, as in 9.2.2).
        QVERIFY(QMetaObject::invokeMethod(calculator,"edit",Q_ARG(QVariant,QVariant("copy"))));QCOMPARE(copied.count(),2);
        // Keyboard entry must still work after the mouse uses a key.
        QVERIFY(click("calculator-key-enter"));
        QTest::keyClick(&view,Qt::Key_1);QTest::keyClick(&view,Qt::Key_Plus);
        QTest::keyClick(&view,Qt::Key_1);QTest::keyClick(&view,Qt::Key_Return);
        QCOMPARE(calculator->property("display").toString(),QString("2"));
        commandKey(&view,Qt::Key_C,46);QCOMPARE(copied.last()[0].toString(),QString("2"));
        commandKey(&view,Qt::Key_D,32);commandKey(&view,Qt::Key_Backspace,14);
        QTest::keyClick(&view,Qt::Key_F2);QTest::qWait(100);
        QVERIFY(!items->busy());QCOMPARE(operations.count(),0);QVERIFY(QFileInfo::exists(document.fileName()));
        QVERIFY(!view.rootObject()->findChild<QObject *>("desktop-name-dialog")->property("visible").toBool());
        QVERIFY(dispatch("calculator"));QCOMPARE(visualItem(view.contentItem(),"calculator-window"),calculator);
        QTest::keyClick(&view,Qt::Key_C);QCOMPARE(calculator->property("display").toString(),QString("0"));
        QTest::keyClick(&view,Qt::Key_2);
        QTest::keyClick(&view,Qt::Key_Escape);QVERIFY(calculator->isVisible());
        QCOMPARE(calculator->property("display").toString(),QString("0"));
        QVERIFY(view.grabWindow().save(QCoreApplication::applicationDirPath()+"/artifacts/calculator.png"));
        commandKey(&view,Qt::Key_W,17);QTRY_VERIFY(!calculator->isVisible());
        QCOMPARE(closed.count(),1);
        QVERIFY(dispatch("control-panels-window"));
        auto *panels=visualItem(view.contentItem(),"control-panels-window");QVERIFY(panels);
        QTRY_VERIFY(panels->hasActiveFocus());
        QVERIFY(dispatch("calculator"));QTRY_VERIFY(calculator->hasActiveFocus());
        commandKey(&view,Qt::Key_W,17);QTRY_VERIFY(panels->hasActiveFocus());
        QCOMPARE(closed.count(),2);
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
