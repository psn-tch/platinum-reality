#include "appearance/interface_font.hpp"
#include "clipboard.hpp"
#include "applications/desktop_entry.hpp"
#include "manager.hpp"
#include "finder/file_operations.hpp"
#include "menu_bridge.hpp"
#include "view_lifecycle.hpp"
#include "graphics_integration.hpp"
#include "output_geometry.hpp"
#include "display_topology_feed.hpp"
#include "x11_bridge.hpp"
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include <QQmlContext>
#include <QScreen>
#include <QScopeGuard>
#include <QTimer>
#include <QWaylandQuickOutput>
#include <QWaylandXdgDecorationManagerV1>
#include <memory>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setFont(classicInterfaceFont());
    QCoreApplication::setApplicationName("Platinum Reality");
    const bool preview = app.arguments().contains("--preview");
    if (!preview && qEnvironmentVariable("FRANKENSTEIN_ISOLATED") != "1")
    {
        qCritical("Use scripts/run-nested.sh; this prototype requires a separate session bus and "
                  "profile.");
        return 2;
    }
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"socket", "Private Wayland socket", "name",
                      QString("frankenstein-%1").arg(QCoreApplication::applicationPid())});
    parser.addOption({"workspace", "Native workspace directory", "path",
                      QDir::currentPath() + "/var/workspace"});
    parser.addOption(
        {"profile", "Separate profile directory", "path", QDir::currentPath() + "/var/profile"});
    parser.addOption({"state-file", "Write runtime observation JSON", "path"});
    parser.addOption({"preview", "Render the shell without starting a display server"});
    parser.addOption({"screenshot", "Save the shell rendering to an image", "path"});
    parser.addOption({"exit-after", "Exit automatically after milliseconds (smoke tests)", "ms"});
    parser.addOption({"no-finder", "Do not start the Finder client"});
    parser.addOption({"fullscreen", "Open the nested desktop full screen"});
    parser.addOption({"application-settings",
                      "Launched applications use the user's 'normal' settings folders or the session's 'private' "
                      "profile (default private)", "mode", "private"});
    parser.addOption({"display-topology", "Experimental owned-session display snapshot (Extend only)", "path"});
    parser.addOption({"trash", "Trash directory shared with other file managers (default: the profile's own)", "path"});
    parser.addOption({"volume", "A further folder the Finder may work in (mounted drives)", "path"});
    parser.addOption({"applications-dir",
                      "Application metadata directory (repeatable; replaces discovery roots)",
                      "path"});
    parser.process(app);
    const auto workspace = QDir(parser.value("workspace")).absolutePath();
    if (parser.isSet("trash"))
        FinderPlaces::trash = QDir::cleanPath(QDir(parser.value("trash")).absolutePath());
    FinderPlaces::volumes = parser.values("volume");
    QDir().mkpath(workspace);
    // The display must outlive the view's renderer and its client buffers.
    std::unique_ptr<PlatinumCompositor> compositor;
    std::unique_ptr<QWaylandXdgShell> shell;
    std::unique_ptr<QWaylandXdgDecorationManagerV1> decoration;
    std::unique_ptr<DesktopOutputGeometry> outputGeometry;
    std::unique_ptr<NativeMenuBridge> menus;
    std::unique_ptr<X11Bridge> x11;
    DesktopClientRetirement retireClients;
    X11BridgeRetirement retireX11;
    QQuickView view;
    view.setTitle("Platinum Reality — Native Desktop");
    view.resize(parser.isSet("fullscreen") && view.screen() ? view.screen()->size()
                                                            : QSize(1120, 760));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    if (!preview)
    {
        configureCompositorGraphics();
        compositor = std::make_unique<PlatinumCompositor>();
        retireClients.setCompositor(compositor.get());
        compositor->setSocketName(parser.value("socket").toUtf8());
        shell = std::make_unique<QWaylandXdgShell>(compositor.get());
        decoration = std::make_unique<QWaylandXdgDecorationManagerV1>();
        decoration->setExtensionContainer(compositor.get());
        decoration->setPreferredMode(QWaylandXdgToplevel::ServerSideDecoration);
        compositor->create();
        shell->initialize();
        decoration->initialize();
        outputGeometry = std::make_unique<DesktopOutputGeometry>(compositor.get(), &view);
        menus = std::make_unique<NativeMenuBridge>(compositor.get());
        x11 = std::make_unique<X11Bridge>();
        retireX11.bridge = x11.get();
    }
    NativeManager manager(
        compositor.get(), &view, workspace, parser.value("profile"), parser.value("state-file"),
        parser.isSet("applications-dir") ? parser.values("applications-dir")
                                         : frankenstein::applications::applicationDirectories());
    const auto cleanupView = qScopeGuard([&] { releaseDesktopView(view); });
    const auto applicationSettings = parser.value("application-settings");
    if (applicationSettings != "normal" && applicationSettings != "private")
    {
        qCritical("--application-settings must be 'normal' or 'private'.");
        return 2;
    }
    manager.setNormalApplicationSettings(applicationSettings == "normal");
    if (shell)
        manager.attachShell(shell.get());
    if (menus)
        QObject::connect(menus.get(), &NativeMenuBridge::endpointChanged, &manager,
                         &NativeManager::setMenuEndpoint);
    if (x11) {
        manager.setX11Bridge(x11.get());
        x11->start(compositor->socketName());
    }
    view.rootContext()->setContextProperty("manager", &manager);
    view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
    if (view.status() == QQuickView::Error)
        return 3;
    bool topologyReady=false;
    quint64 appliedGeneration=0;
    std::unique_ptr<DisplayTopologyFeed> topology;
    if(parser.isSet("display-topology")) {
        topology=std::make_unique<DisplayTopologyFeed>(parser.value("display-topology"));
        QObject::connect(topology.get(),&DisplayTopologyFeed::errorOccurred,&manager,
                         [](const QString &message){qWarning().noquote()<<"Display topology:"<<message;});
        const auto reconcile=[&] {
            if(!topology->snapshot()) return;
            const auto &snapshot=*topology->snapshot();
            // Mirror presentation/input is still experimental; never silently
            // crop a canvas or claim a working Mirror session from metadata.
            if(snapshot.layout.mode()!=DisplayLayout::Mode::Extend) {
                qWarning("This experimental runtime supports Extend only; Mirror presentation is not enabled.");
                if(!topologyReady) app.exit(4);
                return;
            }
            if(view.size()!=snapshot.layout.hostBounds().size() || appliedGeneration==snapshot.generation) return;
            manager.releasePointerCapture();
            if(outputGeometry && !outputGeometry->setDisplayLayout(snapshot.layout)) return;
            if(outputGeometry && !outputGeometry->ready()) return;
            manager.setDisplayLayout(snapshot.layout);
            if(!topology->acknowledge(snapshot.generation)) return;
            appliedGeneration=snapshot.generation;
            if(!topologyReady) {
                topologyReady=true;
                if(!preview && !parser.isSet("no-finder"))
                    QTimer::singleShot(150,&manager,[&]{manager.launchFinder();});
            }
            qInfo()<<"Display topology applied"<<snapshot.generation<<snapshot.layout.primary()<<snapshot.layout.hostBounds();
        };
        const auto schedule=[&,reconcile]{QTimer::singleShot(0,topology.get(),reconcile);};
        if(outputGeometry)
            QObject::connect(outputGeometry.get(),&DesktopOutputGeometry::layoutReady,topology.get(),schedule);
        QObject::connect(topology.get(),&DisplayTopologyFeed::changed,topology.get(),schedule);
        QObject::connect(&view,&QWindow::widthChanged,topology.get(),schedule);
        QObject::connect(&view,&QWindow::heightChanged,topology.get(),schedule);
        if(!topology->refresh()) return 4;
        QTimer::singleShot(15000,topology.get(),[&]{
            if(!topologyReady) {
                qCritical("Experimental display topology did not match a usable extended host window.");
                app.exit(4);
            }
        });
    }
    if (parser.isSet("fullscreen"))
        view.showFullScreen();
    else
        view.show();
    if (!topology && !preview && !parser.isSet("no-finder"))
        QTimer::singleShot(150, &manager, [&] { manager.launchFinder(); });
    if (!preview)
        manager.initializeWallet();
    if (parser.isSet("screenshot"))
        QTimer::singleShot(500, &app,
                           [&]
                           {
                               if (!view.grabWindow().save(parser.value("screenshot")))
                                   qWarning("Screenshot failed");
                           });
    if (parser.isSet("exit-after"))
        QTimer::singleShot(parser.value("exit-after").toInt(), &app, &QCoreApplication::quit);
    if (compositor)
        qInfo("Platinum Reality ready on %s", compositor->socketName().constData());
    else
        qInfo("Platinum Reality shell preview (no native clients)");
    const int result = app.exec();
    return result;
}
