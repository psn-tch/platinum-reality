#include "manager.hpp"
#include "applications/desktop_entry.hpp"
#include <unistd.h>
#include "appearance_settings.hpp"
#include "system_controls.hpp"
#include "audio_service.hpp"
#include "bluetooth.hpp"
#include "wifi.hpp"
#include "alert_sounds.hpp"
#include "interface_sounds.hpp"
#include "applications/application_appearance.hpp"
#include "applications/emulator_launch.hpp"
#include "applications/application_catalog.hpp"
#include "applications/launch_environment.hpp"
#include "client_surface_item.hpp"
#include "desktop_items.hpp"
#include "document_service.hpp"
#include "keyboard_broadcast.hpp"
#include "attention.hpp"
#include "clipboard.hpp"
#include "data_control.hpp"
#include "primary_selection.hpp"
#include "command_keys.hpp"
#include "client_cursors.hpp"
#include "session_control.hpp"
#include "icon_store.hpp"
#include "idle_inhibition.hpp"
#include "notifications.hpp"
#include "status_items.hpp"
#include <QQmlEngine>
#include "host_pointer.hpp"
#include "pointer_constraints.hpp"
#include "screenshot_service.hpp"
#include "finder/file_drag.hpp"
#include "menu_broker.hpp"
#include "surface_client.hpp"
#include "x11_bridge.hpp"
#include "wallet_activation.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSysInfo>
#include <QLocale>
#include <QJsonObject>
#include <QJsonArray>
#include <QMouseEvent>
#include <QQmlContext>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTimer>
#include <QWaylandClient>
#include <QWaylandDrag>
#include <QWaylandPointer>
#include <QWaylandQuickItem>
#include <QWaylandQuickOutput>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <algorithm>
#include <QRegularExpression>
#include <memory>
#include <qqml.h>
#include <utility>
#include <wayland-server-core.h>

using namespace frankenstein::policy;
NativeManager::NativeManager(QWaylandQuickCompositor *c, QQuickView *v, QString root,
                             QString profile, QString stateFile, QStringList applicationDirectories,
                             QObject *parent)
    : QAbstractListModel(parent), m_compositor(c), m_view(v),
      m_workspace(QDir(root).absolutePath()), m_profile(QDir(profile).absolutePath()),
      m_stateFile(std::move(stateFile))
{
    static const int clientSurfaceType =
        qmlRegisterType<ClientSurfaceItem>("Frankenstein", 1, 0, "NativeClientSurfaceItem");
    Q_UNUSED(clientSurfaceType);
    m_state.session = {qulonglong(QDateTime::currentMSecsSinceEpoch())};
    QDir().mkpath(m_profile);
    {
        // The Monitors panel's saved choice (see setUseAllDisplays); anything else means one display.
        QFile displays(m_profile + "/displays.json");
        if (!QFileInfo(displays.fileName()).isSymLink() && displays.open(QIODevice::ReadOnly) &&
            displays.size() <= 4096)
            m_useAllDisplays =
                QJsonDocument::fromJson(displays.readAll()).object().value("mode").toString() == "extend";
    }
    m_appearance = new AppearanceSettings(m_profile, this);
    m_screenshots = new ScreenshotService(m_view, this);
    m_systemControls = new SystemControls(QCoreApplication::applicationDirPath()+"/system_controls.py", this);
    m_audio = new AudioService(QCoreApplication::applicationDirPath()+"/audio_controls.py", this);
    // Only a real session talks to the host's BlueZ. Native tests run against a private or
    // blocked system bus; without one they must never register a pairing agent on the host.
    const bool hostBluetooth = c != nullptr && (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") != "1" ||
                                                qEnvironmentVariableIsSet("DBUS_SYSTEM_BUS_ADDRESS"));
    // Inside another desktop (a window in KDE), that desktop keeps answering pairing questions.
    m_bluetooth = new BluetoothService(hostBluetooth ? QDBusConnection::systemBus() : QDBusConnection(QString()),
                                       qEnvironmentVariable("PLATINUM_LOGIN_SESSION") == "1", this);
    // Under the same rule: tests never register a password agent with the host's iwd.
    m_wifi = new WifiService(hostBluetooth ? QDBusConnection::systemBus() : QDBusConnection(QString()), this);
    // And it never puts the host to sleep or shuts it down.
    m_power = new PowerControl(hostBluetooth ? QDBusConnection::systemBus() : QDBusConnection(QString()), this);
    connect(m_power, &PowerControl::finished, this, [this](bool ok, const QString &error)
            {
                const bool ending = m_pendingPower && *m_pendingPower != PowerControl::Action::Sleep;
                m_pendingPower.reset();
                if (ok && ending)
                {
                    m_allowExit = true;
                    QCoreApplication::quit();
                    return;
                }
                if (ending)
                    m_quitting = false;
                if (!ok)
                    emit errorOccurred(error);
            });
    // Alerts make sound only in a real session; tests and previews flash the menu bar instead, so
    // no test ever plays through the host's speakers. Personal sounds: <data>/platinum-reality/assets.
    const bool audible = c != nullptr && qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") != "1";
    auto data = qEnvironmentVariable("XDG_DATA_HOME");
    if (!QDir::isAbsolutePath(data))
        data = QDir::homePath() + "/.local/share";
    m_personalAssets = data + "/platinum-reality/assets";
    // The real battery only in a real session, like sound; tests may name a fake sysfs tree.
    m_battery = new BatteryStatus(qEnvironmentVariable("FRANKENSTEIN_POWER_SUPPLY_ROOT",
                                                       audible ? "/sys/class/power_supply" : ""), this);
    auto *alerts = new AlertSounds(m_profile, data + "/platinum-reality/assets",
                                   audible ? QString("/usr/bin/pw-play") : QString(), this);
    m_alertSounds = alerts;
    m_interfaceSounds = new InterfaceSounds(m_profile, data + "/platinum-reality/assets",
                                            audible ? QString("/usr/bin/pw-play") : QString(),
                                            [alerts] { return alerts->volume(); }, this);
    connect(m_appearance, &AppearanceSettings::errorOccurred, this, &NativeManager::errorOccurred);
    m_catalog = new ApplicationCatalog(std::move(applicationDirectories), m_workspace, m_profile,
                                       QCoreApplication::applicationDirPath(), this);
    connect(m_catalog, &ApplicationCatalog::refreshed, this, &NativeManager::drainDocumentRequests);
    connect(this, &NativeManager::contextChanged, this, &NativeManager::drainDocumentRequests);
    if (m_compositor)
        new DesktopDocumentService(this);
    m_broker = new MenuBroker(c != nullptr, this);
    m_desktopItems = new DesktopItems(m_workspace, m_profile, this, m_view->size());
    connect(m_desktopItems, &DesktopItems::errorOccurred, this, &NativeManager::errorOccurred);
    connect(m_desktopItems, &DesktopItems::dragChanged, this,
            [this]
            {
                if (!m_desktopItems->dragging())
                    updateDesktopDragTarget(-1, -1);
            });
    connect(m_desktopItems, &DesktopItems::openRequested, this,
            [this](const QString &kind, const QString &path)
            {
                if (kind == "trash")
                {
                    launchTrash();
                    return;
                }
                if (kind == "launcher")
                {
                    launchDesktopFile(path);
                    return;
                }
                FileOperations operations(m_workspace, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes);
                if (operations.readableDirectory(path))
                {
                    launchFinder(path);
                    return;
                }
                requestOpenDocument(path);
            });
    {
        // Apple menu > Recent Applications. Bounded, validated, profile-local.
        QFile recent(m_profile + "/recent-applications.json");
        if (recent.open(QIODevice::ReadOnly) && recent.size() <= 16 * 1024)
            for (const auto &value : QJsonDocument::fromJson(recent.readAll()).array())
            {
                const auto entry = value.toObject();
                if (entry.value("id").isString() && entry.value("name").isString() &&
                    m_recentApplications.size() < 10)
                    m_recentApplications.push_back(QVariantMap{
                        {"id", entry.value("id").toString().left(512)},
                        {"name", entry.value("name").toString().left(256)}});
            }
    }
    m_desktopApplication = {++m_nextApp};
    m_applicationIds.insert("org.frankenstein.Finder", m_desktopApplication);
    m_applicationNames[m_desktopApplication.value] = "Finder";
    dispatch(RegisterApplication{m_desktopApplication, true});
    dispatch(ActivateDesktop{m_desktopApplication});
    m_view->installEventFilter(this);
    // Command editing keys in the shell's own text fields (command_keys.hpp).
    m_view->installEventFilter(new ShellTextCommands(this));
    // Display requests go to the private session supervisor; absent outside that session.
    m_sessionControl = new SessionControl(this);
    connect(m_sessionControl, &SessionControl::busyChanged, this, &NativeManager::displayRequestChanged);
    connect(m_sessionControl, &SessionControl::finished, this, [this](bool ok, const QVariantMap &)
            {
                if (!ok)
                    emit errorOccurred("The main display could not be changed.");
            });
    // Flash drives: mounted as they are plugged in only in the whole session (a desktop in a window
    // leaves that to the desktop around it), and, like Bluetooth, never from a test's host bus.
    m_removableMedia = new RemovableMedia(hostBluetooth ? QDBusConnection::systemBus() : QDBusConnection(QString()),
                                          m_sessionControl->available(), this);
    connect(m_removableMedia, &RemovableMedia::mounted, m_desktopItems, &DesktopItems::refresh);
    connect(m_removableMedia, &RemovableMedia::ejected, m_desktopItems, &DesktopItems::refresh);
    connect(m_removableMedia, &RemovableMedia::failed, this, &NativeManager::errorOccurred);
    const auto refitFullScreen = [this] { refitFullScreens(); emit displayLayoutChanged(); };
    connect(m_view, &QWindow::widthChanged, this, refitFullScreen);
    connect(m_view, &QWindow::heightChanged, this, refitFullScreen);
    connect(m_view, &QWindow::screenChanged, this,
            [this] { QTimer::singleShot(0, this, &NativeManager::hostScaleChanged); });
    if (m_compositor)
    {
        new KeyboardBroadcast(m_compositor, this);
        auto *clipboard = new Clipboard(m_compositor, this);
        m_clipboard = clipboard;
        new DataControl(m_compositor, clipboard, this);
        new PrimarySelection(m_compositor, this);
        auto *attention = m_attention = new ApplicationAttention(m_compositor, this);
        // A granted request brings the window forward; any other marks its application.
        connect(attention, &ApplicationAttention::activationGranted, this, [this](QWaylandSurface *surface) {
            if (auto *window = windowForSurface(surface); window && !shellModal())
                activate(window->id(), true);
        });
        connect(attention, &ApplicationAttention::attentionRequested, this, [this](QWaylandSurface *surface) {
            auto *window = windowForSurface(surface);
            const auto record = window ? m_state.windows.find(window->identity) : m_state.windows.end();
            if (record == m_state.windows.end() || m_state.active.application == record->second.application)
                return;
            m_wantsAttention.insert(record->second.application.value);
            emit contextChanged();
        });
        // The bell is the alert sound, or a menu-bar flash when alerts are silent.
        connect(attention, &ApplicationAttention::bellRung, this, [this] {
            if (auto *sounds = qobject_cast<AlertSounds *>(m_alertSounds))
                sounds->play();
        });
        new ClientCursors(m_compositor, this);
        // Applications on the session bus: tray items and notifications, with their images
        // served to QML as image://platinum/...
        auto icons = std::make_shared<IconStore>();
        m_view->engine()->addImageProvider("platinum", new IconStoreProvider(icons));
        m_notifications = new NotificationServer(true, icons, this);
        // Acting on an alert lets its application come forward.
        m_notifications->setActivationTokens([attention = QPointer<ApplicationAttention>(m_attention)] {
            return attention ? attention->grantedToken() : QString();
        });
        m_statusItems = new StatusItems(true, icons, this);
        m_idle = new IdleInhibition(m_compositor, m_view, true, this);
        connect(m_idle, &IdleInhibition::inhibitedChanged, this, &NativeManager::idleInhibitionChanged);
        const auto updateIdlePreference = [this] { m_idle->setUserKeepsAwake(m_appearance->keepScreenAwake()); };
        connect(m_appearance, &AppearanceSettings::changed, m_idle, updateIdlePreference);
        updateIdlePreference();
        if (!m_notifications->registered())
            qWarning("Notifications are unavailable: another server owns org.freedesktop.Notifications.");
        if (!m_statusItems->registered())
            qWarning("Tray items are unavailable: another host owns org.kde.StatusNotifierWatcher.");
        m_pointerConstraints = new PointerConstraints(m_compositor, new HostPointer(m_view, this), this);
        connect(m_pointerConstraints, &PointerConstraints::capturedChanged, this,
                &NativeManager::pointerCaptureChanged);
        // Menus, modal dialogs and desktop drags need the pointer: deactivate any capture.
        const auto permitCapture = [this]
        { m_pointerConstraints->setPermitted(!shellModal() && !menuOpen() && !m_nativeDragging); };
        connect(this, &NativeManager::contextChanged, this, permitCapture);
        connect(this, &NativeManager::nativeDragChanged, this, permitCapture);
        connect(m_compositor, &QWaylandCompositor::surfaceCreated, this,
                [this](QWaylandSurface *surface)
                {
                    // Public API has a subsurface-position signal but no getter.
                    // Capture it before first mapping, without private Qt headers.
                    surface->setProperty("frankensteinSubsurfacePosition", QPointF());
                    connect(surface, &QWaylandSurface::subsurfacePositionChanged, this,
                            [surface](const QPoint &position)
                            {
                                surface->setProperty("frankensteinSubsurfacePosition",
                                                     QPointF(position));
                            });
                });
        m_dropTrace = wl_display_add_protocol_logger(
                m_compositor->display(),
                [](void *data, wl_protocol_logger_type direction,
                   const wl_protocol_logger_message *message)
                {
                    auto *manager = static_cast<NativeManager *>(data);
                    const QByteArray interface = wl_resource_get_class(message->resource);
                    if(interface=="wl_surface" && direction==WL_PROTOCOL_LOGGER_REQUEST) {
                        auto *surface=QWaylandSurface::fromResource(message->resource);
                        if(!surface) return;
                        const QByteArrayView operation(message->message->name);
                        if(operation=="attach")
                            surface->setProperty("platinumPendingNullBuffer",!message->arguments[0].o);
                        else if(operation=="commit") {
                            const auto pending=surface->property("platinumPendingNullBuffer");
                            if(pending.isValid()) {
                                surface->setProperty("platinumCommittedNullBuffer",pending);
                                surface->setProperty("platinumPendingNullBuffer",QVariant());
                            }
                        }
                        return;
                    }
                    if(qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST")!="1") return;
                    if (!interface.startsWith("wl_data_") || manager->m_dropTraceMessages >= 200)
                        return;
                    ++manager->m_dropTraceMessages;
                    pid_t pid = 0;
                    wl_client_get_credentials(wl_resource_get_client(message->resource), &pid,
                                              nullptr, nullptr);
                    // Names and peer IDs only: never print MIME payloads, paths or tokens.
                    const auto operation = QByteArrayView(message->message->name);
                    const bool mimeArgument =
                        (interface == "wl_data_source" && operation == "offer") ||
                        (interface == "wl_data_offer" &&
                         (operation == "offer" || operation == "receive"));
                    const char *format = "-";
                    if (mimeArgument && message->arguments_count > 0 && message->arguments[0].s)
                    {
                        const auto mime = QByteArrayView(message->arguments[0].s);
                        format = mime == finderDragMime                          ? "drag-token"
                                 : mime == finderFileUrlsMime                    ? "owned-file-urls"
                                 : mime == "text/uri-list"                       ? "standard-urls"
                                 : mime == "application/vnd.portal.filetransfer" ? "portal-files"
                                                                                 : "other";
                    }
                    qInfo("Drop transport %s %s.%s peer=%d format=%s",
                          direction == WL_PROTOCOL_LOGGER_REQUEST ? "request" : "event",
                          interface.constData(), message->message->name, int(pid), format);
                },
                this);
        auto *drag = m_compositor->defaultSeat()->drag();
        connect(drag, &QWaylandDrag::dragStarted, this,
                [this, drag]
                {
                    m_dragOrigin = drag->origin();
                    m_nativeDragging = bool(m_dragOrigin);
                    m_pendingDesktopDrop = false;
                    ++m_dropGeneration;
                    const auto generation = m_dropGeneration;
                    if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST")=="1")
                        qInfo("Drop started generation=%llu origin=%d", generation,
                              int(liveSurfaceClientPid(m_dragOrigin)));
                    if (m_dragOrigin)
                        connect(m_dragOrigin, &QObject::destroyed, this,
                                [this, drag, generation]
                                {
                                    if (!m_nativeDragging || generation != m_dropGeneration)
                                        return;
                                    drag->cancelDrag();
                                    drag->drop();
                                    m_nativeDragging = false;
                                    m_pendingDesktopDrop = false;
                                    m_desktopItems->clearExternalDrag();
                                    emit nativeDragChanged();
                                    queueClientFlush();
                                });
                    emit nativeDragChanged();
                });
        connect(drag, &QWaylandDrag::iconChanged, this,
                [this, drag]
                {
                    if (!drag->origin())
                    {
                        m_nativeDragging = false;
                        m_dragOrigin.clear();
                        m_desktopItems->clearExternalDrag();
                    }
                    emit nativeDragChanged();
                });
        QTimer::singleShot(0, this, &NativeManager::startDropReceiver);
    }
}
NativeManager::~NativeManager()
{
    if (m_dropTrace)
        wl_protocol_logger_destroy(std::exchange(m_dropTrace, nullptr));
    m_retiringDropReceiver = true;
    // This helper has no user documents. Retire only this process we started;
    // ordinary applications keep their existing save/cancel shutdown policy.
    if (m_dropProcess && m_dropProcess->state() != QProcess::NotRunning)
    {
        m_dropProcess->terminate();
        if (!m_dropProcess->waitForFinished(1000))
        {
            m_dropProcess->kill();
            m_dropProcess->waitForFinished(1000);
        }
    }
}
void NativeManager::startDropReceiver()
{
    if (!m_compositor || m_dropProcess)
        return;
    m_dropProcess = new QProcess(this);
    m_dropProcess->setProcessEnvironment(nativeApplicationEnvironment(
        QProcessEnvironment::systemEnvironment(), QString::fromUtf8(m_compositor->socketName()),
        m_profile, QCoreApplication::applicationDirPath() + "/plugins"));
    m_dropProcess->setProgram(QCoreApplication::applicationDirPath() +
                              "/frankenstein-drop-receiver");
    connect(m_dropProcess, &QProcess::readyReadStandardError, this,
            [this]
            {
                const auto output = m_dropProcess->readAllStandardError();
                if (m_dropTrace && m_dropTraceMessages < 200)
                {
                    ++m_dropTraceMessages;
                    qInfo().noquote() << "Drop receiver:" << QString::fromUtf8(output.left(4096));
                }
            });
    connect(m_dropProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error)
            {
                if (!m_retiringDropReceiver && !m_quitting && error == QProcess::FailedToStart)
                    emit errorOccurred("The desktop drop receiver could not start: " +
                                       m_dropProcess->errorString());
                // Unexpected exits are reported by finished(). Our own SIGTERM
                // during teardown is expected and is not a startup failure.
            });
    connect(m_dropProcess, &QProcess::finished, this,
            [this](int, QProcess::ExitStatus)
            {
                if (!m_retiringDropReceiver && !m_quitting)
                    emit errorOccurred("The desktop drop receiver stopped. Restart the nested "
                                       "session to restore folder-to-desktop drops.");
            });
    m_dropProcess->start();
}
QObject *NativeManager::nativeDragIcon() const
{
    return m_nativeDragging && m_compositor ? m_compositor->defaultSeat()->drag()->icon() : nullptr;
}
namespace
{
QQuickItem *visualChild(QQuickItem *item, const QString &name)
{
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = visualChild(child, name))
            return found;
    return nullptr;
}
bool executableIs(qint64 pid, const QString &binary)
{
    const auto expected =
        QFileInfo(QCoreApplication::applicationDirPath() + '/' + binary).canonicalFilePath();
    return pid > 0 && !expected.isEmpty() &&
           QFileInfo(QString("/proc/%1/exe").arg(pid)).symLinkTarget() == expected;
}
} // namespace
ManagedWindow *NativeManager::desktopDropWindowAt(QPointF point, QPointF *local) const
{
    if (!m_compositor || shellModal() || menuOpen())
        return nullptr;
    auto windows = m_windows;
    std::sort(windows.begin(), windows.end(), [](auto *a, auto *b) { return a->z > b->z; });
    for (auto *window : windows)
    {
        if (!window->shown())
            continue;
        auto *frame = visualChild(m_view->rootObject(), "frame-" + QString::number(window->id()));
        if (!frame || !frame->contains(frame->mapFromScene(point)))
            continue;
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualChild(frame, "client-" + QString::number(window->id())));
        if (window->collapsed() || window->shadeInMotion || !client ||
            !client->contains(client->mapFromScene(point)) ||
            !executableIs(liveSurfaceClientPid(client->surface()), "frankenstein-finder"))
            return nullptr; // Never drop through covering chrome or another application.
        if (local)
            *local = client->mapToSurface(client->mapFromScene(point));
        return window;
    }
    return nullptr;
}
void NativeManager::updateDesktopDragTarget(int x, int y)
{
    auto *target = m_desktopItems->dragging() ? desktopDropWindowAt({qreal(x), qreal(y)}) : nullptr;
    const auto id = target ? target->id() : 0;
    if (m_desktopDropWindow != id)
    {
        m_desktopDropWindow = id;
        emit desktopDropTargetChanged();
    }
}
void NativeManager::finishDesktopDrag(int x, int y, bool copy)
{
    if (!m_desktopItems->dragging())
        return;
    QPointF local;
    auto *target = desktopDropWindowAt({qreal(x), qreal(y)}, &local);
    m_desktopDropWindow = 0;
    emit desktopDropTargetChanged();
    if (!target)
    {
        QVariant overWindow;
        QMetaObject::invokeMethod(m_view->rootObject(), "isWindowPoint",
                                  Q_RETURN_ARG(QVariant, overWindow), Q_ARG(QVariant, x),
                                  Q_ARG(QVariant, y));
        if (overWindow.toBool() || shellModal() || menuOpen())
            m_desktopItems->cancelDrag();
        else
            m_desktopItems->finishDrag(x, y, copy);
        return;
    }
    auto *surface = target->xdg ? target->xdg->surface() : nullptr;
    if (!surface || !surface->resource() || !m_desktopItems->beginWindowDrop(copy))
    {
        m_desktopItems->cancelDrag();
        return;
    }
    const auto fail = [this](const QString &error)
    { m_desktopItems->completeWindowDrop({}, error); };
    const auto pid = liveSurfaceClientPid(surface);
    const auto surfaceId = wl_resource_get_id(surface->resource());
    const auto service =
        "org.frankenstein.Finder.p" +
        QString::fromLatin1(
            QCryptographicHash::hash(
                (QFileInfo(m_workspace).canonicalFilePath() + '\n' + m_profile).toUtf8(),
                QCryptographicHash::Sha256)
                .toHex()
                .left(24));
    const auto bus = QDBusConnection::sessionBus();
    auto *interface = bus.interface();
    if (!interface)
    {
        fail("The folder connection is unavailable. No files were transferred.");
        return;
    }
    const QDBusReply<QString> owner = interface->serviceOwner(service);
    const QDBusReply<uint> ownerPid =
        owner.isValid() ? interface->servicePid(owner.value()) : QDBusReply<uint>();
    if (!owner.isValid() || !ownerPid.isValid() || qint64(ownerPid.value()) != pid)
    {
        fail("The folder connection changed. No files were transferred.");
        return;
    }
    auto message = QDBusMessage::createMethodCall(owner.value(), "/Finder",
                                                  "org.frankenstein.Finder", "DropDirectory");
    message << surfaceId << qRound(local.x()) << qRound(local.y());
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message, 1500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, safeSurface = QPointer(surface), pid, id = target->id()]
            {
                const QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                auto *window = find(id);
                if (reply.isError() || !safeSurface || !safeSurface->resource() || !window ||
                    !window->xdg || window->xdg->surface() != safeSurface ||
                    liveSurfaceClientPid(safeSurface) != pid || reply.value().size() > 4096)
                {
                    m_desktopItems->completeWindowDrop(
                        {}, "The folder did not accept the drop. No files were transferred.");
                    return;
                }
                m_desktopItems->completeWindowDrop(reply.value());
            });
}
bool NativeManager::routeNativeDrag(QPointF point, Qt::KeyboardModifiers modifiers)
{
    auto *drag = m_compositor->defaultSeat()->drag();
    m_nativeDragPosition = point.toPoint();
    m_nativeDragCopy = !executableIs(liveSurfaceClientPid(m_dragOrigin), "frankenstein-finder") ||
                       bool(modifiers & (Qt::AltModifier | Qt::ControlModifier));
    QWaylandSurface *target = nullptr;
    QPointF local;
    bool desktopTarget = false;
    if (!shellModal() && !menuOpen())
    {
        // Public pick handles client popups. Inactive windows have a first-click
        // activation shield, so also resolve their content by desktop z order.
        auto *output = qobject_cast<QWaylandQuickOutput *>(m_compositor->defaultOutput());
        auto *picked =
            output ? qobject_cast<QWaylandQuickItem *>(output->pickClickableItem(point)) : nullptr;
        if (picked && picked->surface() && picked->isVisible() && picked->isEnabled())
        {
            target = picked->surface();
            local = picked->mapToSurface(picked->mapFromScene(point));
        }
        else
        {
            auto windows = m_windows;
            std::sort(windows.begin(), windows.end(), [](auto *a, auto *b) { return a->z > b->z; });
            bool overWindow = false;
            for (auto *window : windows)
            {
                if (!window->shown())
                    continue;
                auto *frame =
                    visualChild(m_view->rootObject(), "frame-" + QString::number(window->id()));
                if (!frame || !frame->contains(frame->mapFromScene(point)))
                    continue;
                overWindow = true;
                auto *client = qobject_cast<QWaylandQuickItem *>(
                    visualChild(frame, "client-" + QString::number(window->id())));
                if (!window->collapsed() && !window->shadeInMotion && client &&
                    client->contains(client->mapFromScene(point)))
                {
                    target = client->surface();
                    local = client->mapToSurface(client->mapFromScene(point));
                }
                break;
            }
            if (!overWindow && !m_desktopItems->busy() && m_dropSurface &&
                m_dropSurface->hasContent())
            {
                QVariant allowed;
                QMetaObject::invokeMethod(m_view->rootObject(), "isDesktopDropPoint",
                                          Q_RETURN_ARG(QVariant, allowed),
                                          Q_ARG(QVariant, point.x()), Q_ARG(QVariant, point.y()));
                if (allowed.toBool())
                {
                    target = m_dropSurface;
                    local = point;
                    desktopTarget = true;
                }
            }
        }
    }
    if (target && !liveSurfaceClientPid(target))
    {
        target = nullptr;
        desktopTarget = false;
    }
    if (desktopTarget)
        m_desktopItems->externalDragAt(qRound(point.x()), qRound(point.y()));
    else
        m_desktopItems->clearExternalDrag();
    if (m_dropTrace && desktopTarget != m_nativeDragOnDesktop)
        qInfo("Drop desktop target=%d; target peer=%d", desktopTarget,
              int(liveSurfaceClientPid(target)));
    m_nativeDragOnDesktop = desktopTarget;
    emit nativeDragChanged();
    // Explicit null focus on desktop chrome/outside. Qt's built-in Quick route
    // updates client targets only, otherwise retaining the previous offer.
    drag->dragMove(target, local);
    queueClientFlush();
    return desktopTarget;
}
bool NativeManager::receiveDesktopDrop(uint receiverPid, QStringList paths, QString token)
{
    if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST")=="1")
        qInfo("Drop decoded peer=%u files=%lld token=%d pending=%d modal=%d menu=%d", receiverPid,
              qint64(paths.size()), !token.isEmpty(), m_pendingDesktopDrop, shellModal(),
              menuOpen());
    if (!m_dropProcess || qint64(receiverPid) != m_dropProcess->processId() ||
        !m_pendingDesktopDrop || shellModal() || menuOpen())
        return false;
    m_pendingDesktopDrop = false;
    // MIME has been decoded by the receiving client before releasing the
    // source's pointer state or acknowledging completion. Releasing earlier
    // could retire an older-protocol source before its pipe was read.
    if (m_compositor->defaultSeat()->pointer()->isButtonPressed())
        m_compositor->defaultSeat()->sendMouseReleaseEvent(Qt::LeftButton);
    queueClientFlush();
    // Complete only the matching owned source after MIME decoding. Qt's public
    // data-device v1 API has no cross-process finished notification. A stale
    // acknowledgement must not cancel a later drag in the same Finder.
    if (!token.isEmpty() && token.size() <= 64 &&
        executableIs(m_pendingDropSourcePid, "frankenstein-finder"))
    {
        const auto bus = QDBusConnection::sessionBus();
        if (auto *interface = bus.interface())
        {
            const QDBusReply<QStringList> names = interface->registeredServiceNames();
            if (names.isValid())
                for (const auto &name : names.value())
                {
                    if (!name.startsWith("org.frankenstein.Finder.p"))
                        continue;
                    const QDBusReply<uint> pid = interface->servicePid(name);
                    if (!pid.isValid() || qint64(pid.value()) != m_pendingDropSourcePid)
                        continue;
                    auto message = QDBusMessage::createMethodCall(
                        name, "/Finder", "org.frankenstein.Finder", "FinishFileDrag");
                    message << token;
                    bus.asyncCall(message, 5000);
                    break;
                }
        }
    }
    activateDesktop();
    if (paths.isEmpty())
    {
        emit errorOccurred(
            "The drop did not contain supported local files. No file operation was started.");
        return false;
    }
    return m_desktopItems->receiveFiles(std::move(paths), m_pendingDropPosition, m_pendingDropCopy);
}
QObject *NativeManager::menuBroker() const
{
    return m_broker;
}
QObject *NativeManager::desktopItems() const
{
    return m_desktopItems;
}
QObject *NativeManager::applicationCatalog() const
{
    return m_catalog;
}
QObject *NativeManager::appearance() const
{
    return m_appearance;
}
QObject *NativeManager::screenshots() const
{
    return m_screenshots;
}
bool NativeManager::readableDocument(const QString &path) const
{
    return FileOperations(m_workspace, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes).readableFile(path);
}
void NativeManager::requestOpenDocument(QString path, bool chooseApplication)
{
    FileOperations operations(m_workspace, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes);
    if (!operations.readableFile(path))
    {
        emit errorOccurred("This document is unavailable or outside the selected workspace.");
        return;
    }
    path = QFileInfo(path).absoluteFilePath();
    if (m_pendingDocuments.size() >= 64)
    {
        emit errorOccurred("Finish opening the pending documents before opening more.");
        return;
    }
    if (std::none_of(
            m_pendingDocuments.begin(), m_pendingDocuments.end(), [&](const auto &request)
            { return request.path == path && request.chooseApplication == chooseApplication; }))
        m_pendingDocuments.push_back({path, chooseApplication});
    if (!m_catalog->ready())
        m_catalog->refresh();
    drainDocumentRequests();
}
void NativeManager::drainDocumentRequests()
{
    if (m_documentsQueued || m_pendingDocuments.isEmpty() || !m_catalog->ready() || shellModal())
        return;
    m_documentsQueued = true;
    QTimer::singleShot(0, this,
                       [this]
                       {
                           m_documentsQueued = false;
                           if (m_pendingDocuments.isEmpty() || shellModal())
                               return;
                           const auto request = m_pendingDocuments.takeFirst();
                           const auto preferred =
                               request.chooseApplication
                                   ? QString()
                                   : m_catalog->preferredApplication(request.path);
                           if (!preferred.isEmpty())
                               launchApplication(preferred, request.path);
                           else if (readableDocument(request.path))
                               emit openWithRequested(request.path);
                           else
                               emit errorOccurred("The pending document is no longer available.");
                           drainDocumentRequests();
                       });
}
void NativeManager::launchApplication(QString id, QString document, bool remember)
{
    if (shellModal())
        return; // The picker releases its input token before invoking us.
    const auto panel = m_catalog->builtInAction(id);
    if (!panel.isEmpty())
    {
        if (!document.isEmpty() || remember)
        {
            emit errorOccurred("Control panels cannot open documents.");
            return;
        }
        emit controlPanelRequested(panel);
        rememberRecentApplication(id);
        return;
    }
    FileOperations operations(m_workspace, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes);
    if (!document.isEmpty() && !operations.readableFile(document))
    {
        emit errorOccurred("This document is unavailable or outside the selected workspace.");
        return;
    }
    // Workspace .desktop files stay inert even when a user chooses Open With.
    if (document.endsWith(".desktop", Qt::CaseInsensitive) &&
        id != "org.frankenstein.Editor.desktop")
    {
        emit errorOccurred("Application entry files can be inspected in Editor.");
        return;
    }
    const auto environment = nativeApplicationEnvironment(
        QProcessEnvironment::systemEnvironment(),
        m_compositor ? QString::fromUtf8(m_compositor->socketName()) : QString(), m_profile,
        QCoreApplication::applicationDirPath() + "/plugins");
    auto plan = m_catalog->launchPlan(
        id, document.isEmpty() ? QStringList{} : QStringList{document}, environment);
    if (!plan)
    {
        emit errorOccurred(plan.error);
        return;
    }
    QString appearanceError;
    if (!adaptKnownEmulatorLaunch(plan, appearanceError)) {
        emit errorOccurred(appearanceError);
        return;
    }
    // The private Firefox profile only applies to the private application settings.
    if (!m_normalApplicationSettings && !prepareStyledApplication(plan, m_profile, appearanceError))
    {
        emit errorOccurred(appearanceError);
        return;
    }
    const bool launched =
        launch(plan.program, plan.arguments, plan.workingDirectory, plan.existingApplicationSettings);
    if (launched)
        rememberRecentApplication(id);
    if (launched && remember && !document.isEmpty())
    {
        QString error;
        if (!m_catalog->rememberApplication(id, document, error))
            emit errorOccurred(error);
    }
}
void NativeManager::launchDesktopFile(const QString &path)
{
    // Checked again now, not only when the icon was listed: the user's own executable desktop
    // file inside the Desktop folder (KDE's rule); never a link or anyone else's file.
    const QFileInfo file(path);
    const auto desktop = QFileInfo(m_workspace + "/Desktop").canonicalFilePath();
    if (file.suffix() != "desktop" || !file.isFile() || file.isSymLink() || file.ownerId() != getuid() ||
        !file.permission(QFileDevice::ExeOwner) || file.size() > 64 * 1024 || desktop.isEmpty() ||
        QFileInfo(file.absolutePath()).canonicalFilePath() != desktop)
    {
        emit errorOccurred("This launcher is not trusted. Mark it executable to open it from the desktop.");
        return;
    }
    const auto environment = nativeApplicationEnvironment(
        QProcessEnvironment::systemEnvironment(),
        m_compositor ? QString::fromUtf8(m_compositor->socketName()) : QString(), m_profile,
        QCoreApplication::applicationDirPath() + "/plugins");
    const auto entry = frankenstein::applications::readDesktopEntry(file.absoluteFilePath(), file.fileName(), environment);
    if (!entry.unavailable.isEmpty())
    {
        emit errorOccurred(entry.unavailable);
        return;
    }
    const auto plan = frankenstein::applications::planLaunch(entry, {}, m_workspace, environment);
    if (!plan)
    {
        emit errorOccurred(plan.error);
        return;
    }
    launch(plan.program, plan.arguments, plan.workingDirectory, plan.existingApplicationSettings);
}
bool NativeManager::activateDesktop()
{
    if (shellModal())
        return false;
    dispatch(ActivateDesktop{m_desktopApplication});
    return desktopActive();
}
qulonglong NativeManager::beginShellModal()
{
    const auto token = ++m_nextShellModal;
    m_shellModals.insert(token);
    if (m_state.menu)
        endMenu();
    else
        synchronize();
    return token;
}
void NativeManager::endShellModal(qulonglong token)
{
    if (m_shellModals.remove(token))
        synchronize();
}
void NativeManager::setMenuEndpoint(QWaylandSurface *surface, QString service, QString path)
{
    if (!m_menuEndpoints.contains(surface))
        connect(surface, &QObject::destroyed, this,
                [this, surface] { m_menuEndpoints.remove(surface); });
    m_menuEndpoints[surface] = {std::move(service), std::move(path)};
    synchronize();
}
int NativeManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_windows.size());
}
QVariant NativeManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_windows.size() ||
        role != Qt::UserRole + 1)
        return {};
    return QVariant::fromValue(static_cast<QObject *>(m_windows[index.row()]));
}
QHash<int, QByteArray> NativeManager::roleNames() const
{
    return {{Qt::UserRole + 1, "record"}};
}
QString NativeManager::activeApplication() const
{
    return m_state.active.application
               ? m_applicationNames.value(m_state.active.application->value, "Application")
               : "Finder";
}
QVariantList NativeManager::applications() const
{
    QVariantList result;
    for (auto id : m_state.applications_back_to_front)
        result.push_back(QVariantMap{{"id", qulonglong(id.value)},
                                     {"name", m_applicationNames.value(id.value)},
                                     {"hidden", m_state.applications.at(id).hidden},
                                     {"active", m_state.active.application == id},
                                     {"attention", m_wantsAttention.contains(id.value)}});
    return result;
}
ManagedWindow *NativeManager::windowForSurface(QWaylandSurface *surface) const
{
    for (auto *window : m_windows)
        if (surface && window->xdg && window->xdg->surface() == surface)
            return window;
    return nullptr;
}
void NativeManager::attachShell(QWaylandXdgShell *shell)
{
    connect(shell, &QWaylandXdgShell::toplevelCreated, this, &NativeManager::createWindow);
}
void NativeManager::createWindow(QWaylandXdgToplevel *top, QWaylandXdgSurface *xdg)
{
    if (!top || !xdg || !liveSurfaceClientPid(xdg->surface()))
        return;
    if (m_dropProcess && liveSurfaceClientPid(xdg->surface()) == m_dropProcess->processId() &&
        executableIs(m_dropProcess->processId(), "frankenstein-drop-receiver"))
    {
        m_dropSurface = xdg->surface();
        top->sendConfigure(QSize(2, 2), QList<QWaylandXdgToplevel::State>{});
        queueClientFlush();
        return; // Protocol endpoint only; never a focusable desktop window.
    }
    // Initial configure is required before the first mapped buffer.
    top->sendConfigure(QSize(0, 0), QList<QWaylandXdgToplevel::State>{});
    auto mapped = std::make_shared<QPointer<ManagedWindow>>();
    // Requests can precede the first buffer, including when Qt recreates a
    // toplevel after a display change. Observe intent at protocol creation,
    // rather than connecting only after the window has mapped.
    auto requestedFullscreen=std::make_shared<bool>(false);
    auto requestedOutput=std::make_shared<QPointer<QWaylandOutput>>();
    const auto applyFullscreenIntent=[this,safeTop=QPointer(top),mapped,requestedFullscreen,requestedOutput] {
        if(!safeTop) return;
        // Qt's shell-item integration also answers fullscreen with the entire
        // render-window size. Reconcile after its synchronous signal handlers
        // so our monitor policy is the final configure, not a canvas-sized one.
        if(*mapped) setFullScreen(mapped->data(),*requestedFullscreen,requestedOutput->data());
        else {
            const auto states=*requestedFullscreen
                ? QList<QWaylandXdgToplevel::State>{QWaylandXdgToplevel::FullscreenState}
                : QList<QWaylandXdgToplevel::State>{};
            const auto target=requestedScreen(requestedOutput->data());
            safeTop->sendConfigure(*requestedFullscreen?(target.isEmpty()?primaryDisplayRect():target).size():QSize(0,0),states);
            queueClientFlush();
        }
    };
    connect(top,&QWaylandXdgToplevel::setFullscreen,this,
            [this,requestedFullscreen,requestedOutput,applyFullscreenIntent,mapped,surface=QPointer(xdg->surface())](QWaylandOutput *output) {
                *requestedFullscreen=true;
                *requestedOutput=output;
                // Qt's shell-item handler, connected after this one, uses the item's view output
                // when the request names none, and dereferences it. In Extend a client that never
                // bound wl_output has no view output yet: full screen crashed the whole desktop.
                // Give such views the output of the window's own monitor (so even Qt's interim
                // configure has the right size); our policy below still sends the final one.
                if(!output && surface) {
                    auto *fallback=*mapped ? outputForScreen(windowScreen(mapped->data()))
                                           : m_compositor ? m_compositor->defaultOutput() : nullptr;
                    for(auto *view:surface->views())
                        if(!view->output() && fallback) view->setOutput(fallback);
                }
                QTimer::singleShot(0,this,applyFullscreenIntent);
            });
    connect(top,&QWaylandXdgToplevel::unsetFullscreen,this,
            [this,requestedFullscreen,applyFullscreenIntent] {
                *requestedFullscreen=false;
                QTimer::singleShot(0,this,applyFullscreenIntent);
            });
    connect(
        xdg->surface(), &QWaylandSurface::hasContentChanged, this,
        [this, safeTop = QPointer(top), safeXdg = QPointer(xdg), mapped,requestedFullscreen,requestedOutput]
        {
            if (!safeTop || !safeXdg)
                return;
            auto *surface=safeXdg->surface();
            if(surface && *mapped && liveSurfaceClientPid(surface) && !surface->hasContent() &&
               surface->property("platinumCommittedNullBuffer").isValid() &&
               !surface->property("platinumCommittedNullBuffer").toBool()) {
                // Qt6.12's ClientBuffer::hasContent tests the wl_buffer handle,
                // even for a destroyed committed SHM buffer.
                // A resize can destroy that handle, then commit only state and
                // wait for a frame before replacing it. This is not attach(NULL)
                // (observed separately at protocol commit). Keep window/intent and finish
                // its callback instead of unmapping it into a repaint deadlock.
                surface->sendFrameCallbacks();
                queueClientFlush();
                return;
            }
            if (!safeXdg->surface() || !liveSurfaceClientPid(safeXdg->surface()) ||
                !safeXdg->surface()->hasContent())
            {
                if (*mapped)
                    retireWindow(mapped->data());
                mapped->clear();
                *requestedFullscreen=false;
                return;
            }
            if (*mapped)
                return;
            auto *top = safeTop.data();
            auto *xdg = safeXdg.data();
            std::optional<WindowId> parent;
            for (auto *existing : m_windows)
                if (existing->top == top->parentToplevel())
                    parent = existing->identity;
            const QString key = top->appId().isEmpty()
                                    ? QString("client:%1").arg(liveSurfaceClientPid(xdg->surface()))
                                    : top->appId();
            if (!parent && !m_applicationIds.contains(key))
            {
                auto id = ApplicationId{++m_nextApp};
                m_applicationIds.insert(key, id);
                m_applicationNames[id.value] = applicationDisplayName(key, top->title());
                dispatch(RegisterApplication{id});
            }
            auto application =
                parent ? m_state.windows.at(*parent).application : m_applicationIds.value(key);
            auto *client = xdg->surface()->client();
            if (!m_clientApplications.contains(client))
                connect(client, &QObject::destroyed, this,
                        [this, client] { forgetClient(client); });
            m_clientApplications[client].insert(application.value);
            auto *record = new ManagedWindow(this);
            *mapped = record;
            record->clientIdentity = client;
            record->viaX11Bridge = m_x11 && m_x11->ownsWaylandPeer(liveSurfaceClientPid(xdg->surface()));
            record->identity = {++m_nextWindow};
            record->top = top;
            record->xdg = xdg;
            // Every Firefox gets the Platinum frame. With normal application settings (psn_tch,
            // 2026-10-20) their own profile otherwise drew its own title bar: no Platinum title
            // bar or resize box, and its shadow moved the window when focus changed.
            record->classicBrowserFrame = firefoxClient(liveSurfaceClientPid(xdg->surface()));
            QSize size = xdg->windowGeometry().size();
            if (size.isEmpty())
                size = xdg->surface()->destinationSize();
            if (size.isEmpty())
                size = QSize(640, 430);
            const auto primary=primaryDisplayRect();
            record->geometry = {
                {primary.x()+80 + int((m_nextWindow - 1) % 7) * 30,
                 primary.y()+65 + int((m_nextWindow - 1) % 7) * 28},
                {uint(size.width()), uint(size.height())}};
            if (parent && top->isModal())
            {
                const auto &owner = m_state.windows.at(*parent).geometry;
                const auto position=effectiveDisplayLayout().reachablePosition(
                    {owner.position.x+(int(owner.observed_size.width)-size.width())/2,
                     owner.position.y+(int(owner.observed_size.height)-size.height())/2},size,true);
                record->geometry.position={position.x(),position.y()};
            }
            beginInsertRows({}, int(m_windows.size()), int(m_windows.size()));
            m_windows.push_back(record);
            endInsertRows();
            m_interfaceSounds->play("wopn");
            const bool canShade = record->serverDecorated();
            dispatch(RegisterWindow{record->identity, application, record->geometry, canShade,
                                    parent, top->isModal()});
            if (!shellModal())
                dispatch(ActivateWindow{record->identity});
            connect(top, &QWaylandXdgToplevel::titleChanged, record,
                    [this, record, key, application, root = !parent.has_value()]
                    {
                        if (root && m_applicationNames.contains(application.value))
                            m_applicationNames[application.value] = applicationDisplayName(key, record->title());
                        emit record->changed();
                        synchronize();
                    });
            connect(xdg, &QWaylandXdgSurface::windowGeometryChanged, record,
                    [this, record]
                    {
                        if (!record->xdg)
                            return;
                        auto size = record->xdg->windowGeometry().size();
                        if (size.width() > 0 && size.height() > 0)
                            dispatch(ObserveContentSize{record->identity,
                                                        {uint(size.width()), uint(size.height())}});
                        emit record->changed(); // contentOffset
                    });
            connect(top,&QWaylandXdgToplevel::fullscreenChanged,record,[this,record] {
                // Qt's integration restores its saved moveItem position on the
                // normal-state acknowledgement. Apply unplug recovery after it.
                QTimer::singleShot(0,record,[this,record]{rescueRestoredWindow(record);});
            });
            connect(xdg->surface(), &QWaylandSurface::surfaceDestroyed, record,
                    [this, record] { retireWindow(record); });
            connect(top, &QWaylandXdgToplevel::setMinimized, record,
                    [this, record]
                    {
                        auto it = m_state.windows.find(record->identity);
                        if (it != m_state.windows.end())
                            dispatch(HideApplication{it->second.application});
                    });
            connect(top, &QWaylandXdgToplevel::setMaximized, record,
                    [this, record] { zoom(record->id()); });
            connect(top, &QWaylandXdgToplevel::unsetMaximized, record,
                    [this, record]
                    {
                        if (record->zoomed)
                            zoom(record->id());
                    });
            auto relationship = [this, record]
            {
                if (!record->top)
                    return;
                std::optional<WindowId> parent;
                for (auto *candidate : m_windows)
                    if (candidate->top == record->top->parentToplevel())
                        parent = candidate->identity;
                dispatch(SetWindowRelationship{record->identity, parent, record->top->isModal()});
            };
            connect(top, &QWaylandXdgToplevel::parentToplevelChanged, record, relationship);
            connect(top, &QWaylandXdgToplevel::modalChanged, record, relationship);
            connect(top, &QWaylandXdgToplevel::minSizeChanged, record, &ManagedWindow::changed);
            connect(top, &QWaylandXdgToplevel::maxSizeChanged, record, &ManagedWindow::changed);
            // A client fixing its size too big for the screen goes full screen (oversizedFixedWindow).
            const auto fitOversized = [this, record]
            {
                if (!record->fullScreen && oversizedFixedWindow(record))
                    setFullScreen(record, true, nullptr);
            };
            connect(top, &QWaylandXdgToplevel::minSizeChanged, record, fitOversized);
            connect(top, &QWaylandXdgToplevel::maxSizeChanged, record, fitOversized);
            fitOversized();
            connect(top, &QWaylandXdgToplevel::decorationModeChanged, record,
                    [this, record]
                    {
                        if (record->top)
                            dispatch(
                                SetShadeEligibility{record->identity, record->serverDecorated()});
                        emit record->changed(); // A modal's shade policy may stay unchanged.
                    });
            if(*requestedFullscreen) setFullScreen(record,true,requestedOutput->data());
            const auto id = record->id();
            QTimer::singleShot(0, this,
                               [this, id]
                               {
                                   if (auto *record=find(id); record && record->top && !record->fullScreen) {
                                       const QSize size(record->width(),record->height());
                                       const auto screen=windowScreen(record);
                                       const auto fit=boundedContentSize(size.boundedTo(QSize(screen.width()-12,screen.height()-60)),
                                                                        record->top->minSize(),record->top->maxSize());
                                       if (fit!=size) resizeWindow(id,fit.width(),fit.height());
                                       const auto position=effectiveDisplayLayout().reachablePosition({record->x(),record->y()},fit,true);
                                       moveWindow(id,position.x(),position.y());
                                   }
                                   if (find(id) && !shellModal() &&
                                       desired_focus(m_state).window == WindowId{id})
                                       emit focusWindow(id);
                               });
        });
}
void NativeManager::retireWindow(ManagedWindow *record)
{
    auto row = m_windows.indexOf(record);
    if (row < 0)
        return;
    {
        QScopedValueRollback<bool> defer(m_synchronizationDeferred, true);
        std::optional<ApplicationId> application;
        if (const auto found = m_state.windows.find(record->identity); found != m_state.windows.end())
        {
            application = found->second.application;
            dispatch(RemoveWindow{record->identity});
        }
        if (record->top)
            disconnect(record->top, nullptr, record, nullptr);
        if (record->xdg)
        {
            disconnect(record->xdg, nullptr, record, nullptr);
            disconnect(record->xdg->surface(), nullptr, record, nullptr);
        }
        beginRemoveRows({}, row, row);
        m_windows.removeAt(row);
        endRemoveRows();
        m_interfaceSounds->play("wcls");
        if (record->viaX11Bridge && application)
        {
            // Unrelated X11 applications share the bridge's one long-lived
            // Wayland connection. It is not evidence that a closed game is
            // still running. Keep its claim only while it has mapped windows.
            const bool anotherWindow = std::any_of(m_windows.cbegin(), m_windows.cend(), [&](auto *other) {
                const auto found = m_state.windows.find(other->identity);
                return other->clientIdentity == record->clientIdentity && found != m_state.windows.end() &&
                       found->second.application == *application;
            });
            if (!anotherWindow)
            {
                auto claims = m_clientApplications.find(record->clientIdentity);
                if (claims != m_clientApplications.end()) claims->remove(application->value);
                removeUnclaimedApplication(*application);
            }
        }
        record->deleteLater();
    }
    synchronize();
}
QString NativeManager::applicationDisplayName(const QString &key, const QString &title) const
{
    const auto catalogName = m_catalog->applicationName(key);
    if (!catalogName.isEmpty()) return catalogName;
    static const QRegularExpression steam("^steam_app_[0-9]+$");
    if (steam.match(key).hasMatch())
    {
        auto label = title.left(1024);
        for (auto &character : label)
            if (!character.isPrint()) character = QChar(' ');
        label = label.simplified().left(120);
        if (!label.isEmpty() && label != key) return label;
    }
    return key.section('.', -1);
}
void NativeManager::removeUnclaimedApplication(ApplicationId application)
{
    if (application == m_desktopApplication) return;
    const auto found = m_state.applications.find(application);
    if (found != m_state.applications.end() && !found->second.windows_back_to_front.empty()) return;
    for (const auto &claims : m_clientApplications)
        if (claims.contains(application.value)) return;
    if (found != m_state.applications.end()) dispatch(RemoveApplication{application});
    m_applicationNames.remove(application.value);
    for (auto it = m_applicationIds.begin(); it != m_applicationIds.end();)
        if (it.value() == application) it = m_applicationIds.erase(it);
        else ++it;
}
void NativeManager::forgetClient(QWaylandClient *client)
{
    {
        // Remove every window belonging to this connection before applying focus,
        // menu or configure effects. Intermediate policy states can otherwise pick
        // another window whose surface has already lost this same client.
        QScopedValueRollback<bool> defer(m_synchronizationDeferred, true);
        const auto candidates = m_clientApplications.take(client);
        for (auto *record : QList(m_windows))
            if (record->clientIdentity == client)
                retireWindow(record);
        for (auto id : candidates)
            removeUnclaimedApplication({id});
    }
    synchronize();
}
ManagedWindow *NativeManager::find(qulonglong id) const
{
    for (auto *window : m_windows)
        if (window->id() == id)
            return window;
    return nullptr;
}
void NativeManager::dispatch(Event event)
{
    auto result = transition(m_state, {m_state.session, std::move(event)});
    if (result.status == Status::rejected)
    {
        qWarning("Policy rejected event: %d", int(result.rejection));
        return;
    }
    if (result.status == Status::no_change)
        return;
    m_state = std::move(result.next);
    synchronize();
}
void NativeManager::synchronize()
{
    if (m_synchronizationDeferred)
        return;
    // QML animation callbacks can update the input barrier while evaluating a
    // record notification. The outer reconciliation consumes that updated state;
    // nested notifications would re-enter the same collapsed-property binding.
    QScopedValueRollback<bool> reconciling(m_synchronizationDeferred, true);
    const auto order = visible_order(m_state);
    for (auto *record : m_windows)
    {
        auto it = m_state.windows.find(record->identity);
        if (it == m_state.windows.end())
            continue;
        record->geometry = it->second.geometry;
        record->shaded = it->second.collapsed;
        record->visible = !m_state.applications.at(it->second.application).hidden;
        record->shadeAllowed = it->second.shade_eligible;
        record->selected = m_state.active.window == record->identity;
        if (record->top && liveSurfaceClientPid(record->xdg ? record->xdg->surface() : nullptr) &&
            record->configuredActive != record->selected)
        {
            record->configuredActive = record->selected;
            record->top->sendConfigure(record->fullScreen ? record->fullScreenGeometry.size()
                                                          : QSize(record->width(), record->height()),
                                       configureStates(record));
        }
        auto layer = std::find(order.begin(), order.end(), record->identity);
        record->z = layer == order.end() ? 0 : int(layer - order.begin()) + 1;
        emit record->changed();
    }
    // An application that comes forward no longer asks for attention; nor does one that quit.
    if (m_state.active.application)
        m_wantsAttention.remove(m_state.active.application->value);
    for (auto it = m_wantsAttention.begin(); it != m_wantsAttention.end();)
        it = m_state.applications.contains(ApplicationId{*it}) ? std::next(it) : m_wantsAttention.erase(it);
    emit contextChanged();
    auto focus = desired_focus(m_state);
    auto *window = !shellModal() && focus.window ? find(focus.window->value) : nullptr;
    if (window && (window->shadeInMotion ||
                   !liveSurfaceClientPid(window->xdg ? window->xdg->surface() : nullptr)))
        window = nullptr;
    if (m_compositor)
        m_compositor->defaultSeat()->setKeyboardFocus(window && window->xdg ? window->xdg->surface()
                                                                            : nullptr);
    if (window)
        emit focusWindow(window->id());
    else if (desktopActive() && !m_state.menu && !shellModal())
        emit focusDesktop();
    auto *active = m_state.active.window ? find(m_state.active.window->value) : nullptr;
    // Dialogs commonly have no menu endpoint. Resolve only through explicit,
    // same-application ancestry; app labels/PIDs alone never select a menu.
    auto *menuWindow = active;
    for (std::size_t depth = 0; menuWindow && depth < m_state.windows.size(); ++depth)
    {
        auto *candidate = menuWindow->xdg ? menuWindow->xdg->surface() : nullptr;
        const auto endpoint = m_menuEndpoints.value(candidate);
        if (!endpoint.first.isEmpty() && !endpoint.second.isEmpty())
            break;
        const auto current = m_state.windows.find(menuWindow->identity);
        if (current == m_state.windows.end() || !current->second.parent)
            break;
        auto *parent = find(current->second.parent->value);
        if (!parent ||
            m_state.windows.at(parent->identity).application != current->second.application)
            break;
        menuWindow = parent;
    }
    auto *surface = menuWindow && menuWindow->xdg ? menuWindow->xdg->surface() : nullptr;
    const auto endpoint = m_menuEndpoints.value(surface);
    const auto menuPid = liveSurfaceClientPid(surface);
    m_broker->select(menuWindow && menuPid ? menuWindow->id() : 0,
                     menuPid ? endpoint.first : QString(), menuPid ? endpoint.second : QString(),
                     menuPid);
    m_broker->setInteractionContext(active ? active->id() : 0,
                                    shellModal() ||
                                        (active && (active->modal() || active->shadeInMotion)));
    queueClientFlush();
    if (!m_stateFile.isEmpty())
    {
        QSaveFile file(m_stateFile);
        if (file.open(QIODevice::WriteOnly))
        {
            file.write(QJsonDocument::fromVariant(snapshot()).toJson());
            file.commit();
        }
    }
}
void NativeManager::queueClientFlush()
{
    if (!m_compositor || m_flushQueued)
        return;
    m_flushQueued = true;
    QTimer::singleShot(0, this,
                       [this]
                       {
                           m_flushQueued = false;
                           // Qt normally flushes before its event loop blocks. Polling/nested
                           // event loops can keep running without that signal. Flush outgoing
                           // requests after this turn; do not reenter Wayland request dispatch.
                           wl_display_flush_clients(m_compositor->display());
                       });
}
void NativeManager::activate(qulonglong id, bool expand)
{
    if (shellModal())
        return;
    dispatch(ActivateWindow{{id}, expand ? Expansion::restore : Expansion::preserve});
}
void NativeManager::contextClickWindow(qulonglong id, QPointF point)
{
    if (!m_compositor || shellModal() || menuOpen() || m_nativeDragging)
        return;
    activate(id);
    auto *window = find(id);
    if (!window || !window->active() || window->collapsed() || window->shadeInMotion)
        return;
    auto *client = qobject_cast<QWaylandQuickItem *>(
        visualChild(m_view->rootObject(), "client-" + QString::number(id)));
    if (!client || !client->contains(client->mapFromScene(point)))
        return;
    auto *seat = m_compositor->defaultSeat();
    seat->sendMouseMoveEvent(client->view(), client->mapToSurface(client->mapFromScene(point)),
                             point);
    seat->sendMousePressEvent(Qt::RightButton);
    seat->sendMouseReleaseEvent(Qt::RightButton);
    queueClientFlush();
}
void NativeManager::activateApplication(qulonglong id)
{
    if (shellModal())
        return;
    dispatch(ActivateApplication{{id}});
}
void NativeManager::shade(qulonglong id)
{
    if (shellModal())
        return;
    auto it = m_state.windows.find({id});
    if (it != m_state.windows.end())
    {
        m_interfaceSounds->play(it->second.collapsed ? "wexp" : "wcol");
        dispatch(SetCollapsed{{id}, !it->second.collapsed});
    }
}
void NativeManager::setShadeInMotion(qulonglong id, bool running)
{
    auto *window = find(id);
    if (!window || window->shadeInMotion == running)
        return;
    window->shadeInMotion = running;
    synchronize();
    // A triple-click undid the double-click's WindowShade; fill the screen once it has rolled down.
    if (!running && window->fillPending)
    {
        window->fillPending = false;
        fillScreen(id);
    }
}
QVariantList NativeManager::displayOutputs() const
{
    const auto layout = effectiveDisplayLayout();
    QVariantList result;
    static const QStringList internalPrefixes{"eDP", "LVDS", "DSI"};
    for (const auto &output : layout.outputs())
    {
        const bool internal = std::any_of(internalPrefixes.cbegin(), internalPrefixes.cend(),
                                          [&](const QString &prefix) { return output.name.startsWith(prefix); });
        result.append(QVariantMap{{"name", output.name},
                                  {"x", output.logical.x()},
                                  {"y", output.logical.y()},
                                  {"width", output.logical.width()},
                                  {"height", output.logical.height()},
                                  {"scale", output.scale},
                                  {"transform", output.transform},
                                  {"primary", output.name == layout.primary()},
                                  {"internal", internal}});
    }
    return result;
}
bool NativeManager::displayArrangementAvailable() const
{
    return m_displayLayout.has_value() && m_sessionControl && m_sessionControl->available();
}
bool NativeManager::setUseAllDisplays(bool all)
{
    QSaveFile file(m_profile + "/displays.json");
    const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"mode", all ? "extend" : "single"}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        emit errorOccurred("The Monitors setting could not be saved.");
        return false;
    }
    if (m_useAllDisplays != all)
    {
        m_useAllDisplays = all;
        emit useAllDisplaysChanged();
    }
    return true;
}
bool NativeManager::displayRequestBusy() const
{
    return m_sessionControl && m_sessionControl->busy();
}
bool NativeManager::requestPrimaryDisplay(const QString &name)
{
    if (!displayArrangementAvailable() || !m_displayLayout)
        return false;
    const auto &outputs = m_displayLayout->outputs();
    if (std::none_of(outputs.cbegin(), outputs.cend(), [&](const DisplayOutput &o) { return o.name == name; }))
        return false;
    if (name == m_displayLayout->primary())
        return true;
    return m_sessionControl->request("primary " + name.toLatin1() + "\n");
}
DisplayLayout NativeManager::effectiveDisplayLayout() const
{
    return m_displayLayout ? *m_displayLayout : DisplayLayout::single(m_view->size());
}
QRect NativeManager::windowScreen(const ManagedWindow *window) const
{
    return effectiveDisplayLayout().screenForWindow(QRect(window->x(),window->y(),window->width(),window->height()));
}
void NativeManager::refitFullScreens()
{
    const auto layout=effectiveDisplayLayout();
    for(auto *window:m_windows) if(window->fullScreen && window->top) {
        window->fullScreenGeometry=layout.screenForWindow(window->fullScreenGeometry);
        window->top->sendConfigure(window->fullScreenGeometry.size(),configureStates(window));
        emit window->changed();
        queueClientFlush();
    }
}
void NativeManager::setDisplayLayout(const DisplayLayout &layout)
{
    const auto previous=effectiveDisplayLayout();
    m_displayLayout=layout;
    m_desktopItems->setDisplayLayout(layout);
    for(auto *window:m_windows) {
        // Fullscreen content size is observed at the retained normal-window
        // position. Combining those values can select a different monitor at a
        // boundary; the stored fullscreen rectangle identifies the real one.
        const auto oldScreen=window->fullScreen ? window->fullScreenGeometry
            : previous.screenForWindow(QRect(window->x(),window->y(),window->width(),window->height()));
        QPoint position(window->x(),window->y());
        QString connector;
        for(const auto &output:previous.outputs()) if(output.logical==oldScreen) connector=output.name;
        QRect destination;
        if(previous.mode()==DisplayLayout::Mode::Extend && layout.mode()==DisplayLayout::Mode::Extend)
            for(const auto &output:layout.outputs()) if(output.name==connector) destination=output.logical;
        if(!destination.isEmpty()) position+=destination.topLeft()-oldScreen.topLeft();
        const QSize size(window->width(),window->height());
        const auto reachable=layout.reachablePosition(position,size);
        if(window->fullScreen && (destination.isEmpty() || reachable!=position))
            window->restoreNeedsRescue=true;
        if(reachable!=position) position=layout.reachablePosition(position,size,true);
        // A window sized for a bigger monitor that went away shrinks to the one it is now on, as
        // new windows are fitted, with its title bar and resize box on screen (psn_tch, 2026-10-22).
        if(!window->fullScreen && window->top && window->canResize()) {
            const auto screen=layout.screenForWindow(QRect(position,size));
            const auto fit=boundedContentSize(size.boundedTo(QSize(screen.width()-12,screen.height()-60)),
                                              window->top->minSize(),window->top->maxSize());
            if(fit!=size) {
                window->top->sendConfigure(fit,configureStates(window));
                queueClientFlush();
                position=layout.reachablePosition(position,fit,true);
            }
        }
        dispatch(SetPosition{window->identity,{position.x(),position.y()}});
        if(window->fullScreen) window->fullScreenGeometry=destination.isEmpty()?layout.primaryRect():destination;
    }
    refitFullScreens();
    // A client that fixed its size too big for the remaining monitor goes full screen there.
    for(auto *window:m_windows)
        if(!window->fullScreen && oversizedFixedWindow(window))
            setFullScreen(window,true,nullptr);
    emit displayLayoutChanged();
}
void NativeManager::rescueRestoredWindow(ManagedWindow *window)
{
    if(!window || !window->restoreNeedsRescue || window->fullScreen || !window->top || window->top->fullscreen())
        return;
    window->restoreNeedsRescue=false;
    const auto size=window->restoreSize.isValid()?window->restoreSize:QSize(window->width(),window->height());
    const auto position=effectiveDisplayLayout().reachablePosition({window->x(),window->y()},size,true);
    dispatch(SetPosition{window->identity,{position.x(),position.y()}});
}
void NativeManager::moveWindow(qulonglong id, int x, int y)
{
    if (shellModal())
        return;
    const auto *window=find(id);
    if(!window) return;
    const auto position=effectiveDisplayLayout().reachablePosition({x,y},{window->width(),window->height()});
    dispatch(SetPosition{{id},{position.x(),position.y()}});
}
void NativeManager::resizeWindow(qulonglong id, int width, int height)
{
    auto *w = find(id);
    if (!w || !w->top || !w->canResize() || w->fullScreen || w->shadeInMotion || shellModal())
        return;
    const auto size = boundedContentSize({width, height}, w->top->minSize(), w->top->maxSize());
    w->top->sendConfigure(size, configureStates(w));
    queueClientFlush();
}
QList<QWaylandXdgToplevel::State> NativeManager::configureStates(const ManagedWindow *w) const
{
    // states() is what the client has acknowledged. A new window's activation
    // or full screen can still be in flight when another configure follows;
    // resend what this manager last decided instead of dropping it.
    auto states = w->top->states();
    states.removeAll(QWaylandXdgToplevel::ActivatedState);
    states.removeAll(QWaylandXdgToplevel::FullscreenState);
    if (w->configuredActive)
        states.push_back(QWaylandXdgToplevel::ActivatedState);
    if (w->fullScreen)
        states.push_back(QWaylandXdgToplevel::FullscreenState);
    return states;
}
QRect NativeManager::requestedScreen(QWaylandOutput *output) const
{
    if (!output || !m_compositor || !m_compositor->outputs().contains(output) ||
        output->property("platinumRetired").toBool())
        return {};
    const auto name=output->property("platinumDisplayName").toString();
    const auto layout=effectiveDisplayLayout();
    for (const auto &description : layout.outputs())
        if (description.name == name)
            return description.logical;
    return {};
}
QWaylandOutput *NativeManager::outputForScreen(const QRect &screen) const
{
    if (!m_compositor)
        return nullptr;
    const auto layout = effectiveDisplayLayout(); // outputs() refers into it: keep it alive.
    for (const auto &description : layout.outputs())
        if (description.logical == screen)
            for (auto *output : m_compositor->outputs())
                if (!output->property("platinumRetired").toBool() &&
                    output->property("platinumDisplayName").toString() == description.name)
                    return output;
    return m_compositor->defaultOutput();
}
bool NativeManager::oversizedFixedWindow(const ManagedWindow *w) const
{
    // A main window whose client fixed its size (minimum = maximum) larger than its screen leaves
    // below the menu bar and a title bar: games (Daggerfall Unity's launcher, 2026-10-22) whose
    // "windowed" size is the screen's. Framed, part of it would be off-screen and unreachable.
    if (!w || !w->top || w->modal() || w->top->parentToplevel())
        return false;
    const auto size = w->top->maxSize();
    if (size.isEmpty() || w->top->minSize() != size)
        return false;
    const auto screen = windowScreen(w);
    const int chrome = (screen == effectiveDisplayLayout().primaryRect() ? 26 : 0) + 22;
    return size.width() > screen.width() || size.height() + chrome > screen.height();
}
void NativeManager::setFullScreen(ManagedWindow *w, bool fullScreen, QWaylandOutput *requested)
{
    if (!w || !w->top)
        return;
    // Too big to show framed (psn_tch, 2026-10-22: such windows open full screen); a request to
    // leave full screen keeps it there while the window cannot fit.
    if (!fullScreen && oversizedFixedWindow(w))
        fullScreen = true;
    // xdg-shell requires a configure in reply even when nothing changes.
    if (fullScreen && !w->fullScreen)
    {
        w->restoreSize = QSize(w->width(), w->height());
        w->fullScreenGeometry = windowScreen(w);
        w->fullScreen = true;
        activate(w->id());
    }
    else if (!fullScreen && w->fullScreen)
        w->fullScreen = false;
    if (fullScreen) {
        const auto target=requestedScreen(requested);
        if(!target.isEmpty()) w->fullScreenGeometry=target;
    }
    const auto size = w->fullScreen ? w->fullScreenGeometry.size()
                      : w->restoreSize.isValid() ? w->restoreSize
                                                 : QSize(w->width(), w->height());
    w->top->sendConfigure(size, configureStates(w));
    emit w->changed();
    queueClientFlush();
    if(!w->fullScreen)
        QTimer::singleShot(0,w,[this,w]{rescueRestoredWindow(w);});
}
void NativeManager::zoom(qulonglong id)
{
    auto *w = find(id);
    if (!w || !w->canZoom() || w->shadeInMotion || shellModal())
        return;
    if (w->zoomed)
    {
        resizeWindow(id, w->userSize.width(), w->userSize.height());
        w->zoomed = false;
        m_interfaceSounds->play("wzmi");
    }
    else
    {
        w->userSize = QSize(w->width(), w->height());
        const auto screen=windowScreen(w);
        resizeWindow(id, std::min(850, screen.width() - 110),
                     std::max(120, screen.height() - 140));
        w->zoomed = true;
        m_interfaceSounds->play("wzmo");
    }
}
void NativeManager::fillScreen(qulonglong id)
{
    auto *w = find(id);
    if (!w || !w->top || !w->canResize() || w->fullScreen || shellModal())
        return;
    if (w->collapsed() && !w->shadeInMotion)
        dispatch(SetCollapsed{{id}, false});
    if (w->shadeInMotion)
    {
        w->fillPending = true;
        return;
    }
    if (w->filled)
    {
        w->filled = false;
        resizeWindow(id, w->filledRestore.width(), w->filledRestore.height());
        moveWindow(id, w->filledRestore.x(), w->filledRestore.y());
        m_interfaceSounds->play("wzmi");
        return;
    }
    // The window's screen, below the menu bar on the main one; the frame's chrome fits inside too.
    const auto screen = windowScreen(w);
    const int top = screen.y() + (screen.contains(effectiveDisplayLayout().primaryRect().topLeft()) ? 26 : 0);
    const int frameWidth = w->serverDecorated() ? 6 : 0, frameHeight = w->serverDecorated() ? 27 : 0;
    w->filledRestore = QRect(w->x(), w->y(), w->width(), w->height());
    w->filled = true;
    w->zoomed = false;
    moveWindow(id, screen.x(), top);
    resizeWindow(id, screen.width() - frameWidth, screen.y() + screen.height() - top - frameHeight);
    m_interfaceSounds->play("wzmo");
}
void NativeManager::closeWindow(qulonglong id)
{
    if (shellModal())
        return;
    activate(id);
    if (m_state.active.window != WindowId{id})
        return;
    auto *w = find(id);
    if (w && w->top)
        w->top->sendClose();
    queueClientFlush();
}
void NativeManager::hideApplication()
{
    if (shellModal())
        return;
    if (m_state.active.application)
        dispatch(HideApplication{*m_state.active.application});
}
void NativeManager::hideOtherApplications()
{
    if (shellModal() || !m_state.active.application)
        return;
    const auto active = *m_state.active.application;
    std::vector<ApplicationId> others;
    for (const auto &[id, application] : m_state.applications)
        if (id != active && !application.hidden)
            others.push_back(id);
    {
        QScopedValueRollback<bool> defer(m_synchronizationDeferred, true);
        for (const auto id : others)
            dispatch(HideApplication{id});
    }
    synchronize();
}
void NativeManager::showAllApplications()
{
    if (shellModal())
        return;
    std::vector<ApplicationId> hidden;
    for (const auto &[id, application] : m_state.applications)
        if (application.hidden)
            hidden.push_back(id);
    {
        QScopedValueRollback<bool> defer(m_synchronizationDeferred, true);
        for (const auto id : hidden)
            dispatch(ShowApplication{id});
    }
    synchronize();
}
void NativeManager::requestQuitActiveApplication()
{
    if (shellModal() || menuOpen() || m_nativeDragging || !m_state.active.application)
        return;
    const auto application = *m_state.active.application;
    if (application == m_desktopApplication && m_desktopItems->busy())
        return;
    // An application dialog owns its save/cancel decision. Do not close it or
    // issue a second batch of requests while it is asking for that decision.
    for (const auto &[_, state] : m_state.windows)
        if (state.application == application && state.modal)
            return;
    // Wayland has no generic application-Quit request. Close its independent
    // toplevels normally; the client decides whether to save, cancel or retain
    // a background process. Descendant utility/dialog windows follow the owner.
    const auto ids = m_state.applications.at(application).windows_back_to_front;
    for (auto iterator = ids.rbegin(); iterator != ids.rend(); ++iterator)
    {
        auto state = m_state.windows.find(*iterator);
        if (state == m_state.windows.end() || state->second.parent)
            continue;
        auto *window = find(iterator->value);
        if (window && window->top)
            window->top->sendClose();
    }
    queueClientFlush();
}
bool NativeManager::pointerCaptureAvailable() const
{
    return m_pointerConstraints && m_pointerConstraints->available();
}
bool NativeManager::pointerCaptured() const
{
    return m_pointerConstraints && m_pointerConstraints->captured();
}
bool NativeManager::idleInhibitAvailable() const
{
    return m_idle && m_idle->available();
}
bool NativeManager::idleInhibited() const
{
    return m_idle && m_idle->inhibited();
}
QObject *NativeManager::notifications() const
{
    return m_notifications;
}
QObject *NativeManager::statusItems() const
{
    return m_statusItems;
}
void NativeManager::releasePointerCapture()
{
    if (m_pointerConstraints)
        m_pointerConstraints->release();
}
void NativeManager::beginMenu()
{
    if (shellModal())
        return;
    if (!m_state.menu && m_state.active.application)
        dispatch(BeginMenuTracking{{++m_nextTracking}});
}
void NativeManager::endMenu()
{
    if (m_state.menu)
        dispatch(EndMenuTracking{m_state.menu->id});
}
void NativeManager::setX11Bridge(X11Bridge *bridge)
{
    Q_ASSERT(!m_x11);
    m_x11 = bridge;
    connect(bridge, &X11Bridge::failed, this, &NativeManager::errorOccurred);
    connect(bridge, &X11Bridge::changed, this, [this] {
        if (m_x11->starting())
            return;
        configureWalletEnvironment();
        drainPendingLaunches();
        synchronize(); // Persist ready/failed display status as well as windows.
        emit contextChanged();
    });
}
void NativeManager::initializeWallet()
{
    // Native tests and windowed/nested sessions never opt in through inherited markers.
    if (!m_compositor || !m_normalApplicationSettings || m_wallet ||
        qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") == "1" ||
        qEnvironmentVariable("PLATINUM_WALLET_REQUESTED") != "1")
        return;
    m_walletRequested = true;
    if (qEnvironmentVariable("PLATINUM_WALLET_SERVICES") != "1")
        return; // Preserve the launcher's reason; recognized apps cannot silently downgrade.
    m_wallet = new WalletActivation(QDBusConnection::sessionBus(), this);
    connect(m_wallet, &WalletActivation::changed, this, [this] {
        if (!m_wallet->pending()) {
            drainPendingLaunches();
            synchronize();
        }
    });
    configureWalletEnvironment();
}
void NativeManager::configureWalletEnvironment()
{
    if (!m_wallet || (m_x11 && m_x11->starting()))
        return;
    auto environment = nativeApplicationEnvironment(
        QProcessEnvironment::systemEnvironment(), QString::fromUtf8(m_compositor->socketName()),
        m_profile, QCoreApplication::applicationDirPath() + "/plugins", true);
    if (m_x11) m_x11->applyTo(environment);
    m_wallet->configure(environment);
}
void NativeManager::drainPendingLaunches()
{
    if ((m_x11 && m_x11->starting()) || (m_wallet && m_wallet->pending()))
        return;
    const auto queued = std::exchange(m_pendingLaunches, {});
    for (const auto &request : queued)
        launch(request.program, request.arguments, request.directory, request.existingSettings);
}
bool NativeManager::launch(QString program, QStringList arguments, QString directory,
                           bool existingApplicationSettings)
{
    if (!m_compositor)
    {
        emit errorOccurred("Applications require the native nested session.");
        return false;
    }
    if ((m_x11 && m_x11->starting()) || (m_wallet && m_wallet->pending())) {
        m_pendingLaunches.push_back({program, arguments, directory, existingApplicationSettings});
        return true;
    }
    QString appearanceError;
    if (!prepareApplicationAppearance(m_profile, appearanceError))
    {
        emit errorOccurred(appearanceError);
        return false;
    }
    // Platinum Reality' own applications (Finder, Editor, Puzzle) keep their state in the session
    // profile; everything else may use the user's own settings folders.
    static const QStringList ownedApplications{"frankenstein-finder", "frankenstein-editor", "frankenstein-puzzle"};
    const QFileInfo executable(program);
    const bool owned = ownedApplications.contains(executable.fileName()) &&
                       executable.canonicalPath() == QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath();
    const bool userSettings = (m_normalApplicationSettings && !owned) || existingApplicationSettings;
    QProcess process;
    auto environment = nativeApplicationEnvironment(
        QProcessEnvironment::systemEnvironment(), QString::fromUtf8(m_compositor->socketName()),
        m_profile, QCoreApplication::applicationDirPath() + "/plugins", userSettings);
    if (m_normalApplicationSettings && !owned)
    {
        // GTK reads the user's own gtk.css; the Platinum look comes from a private theme instead.
        const auto themeData = preparePrivateGtkTheme(m_profile, appearanceError);
        if (themeData.isEmpty())
        {
            emit errorOccurred(appearanceError);
            return false;
        }
        environment.insert("GTK_THEME", privateGtkThemeName);
        const auto dataDirectories = environment.value("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
        environment.insert("XDG_DATA_DIRS", themeData + ":" + dataDirectories);
    }
    if (m_x11)
        m_x11->applyTo(environment);
    const bool walletSettings = m_normalApplicationSettings && !owned;
    if (!configureKnownApplicationBackend(program, arguments, environment, appearanceError,
                                         walletSettings && m_wallet && m_wallet->ready(),
                                         walletSettings && m_walletRequested)) {
        const auto reason = m_wallet ? m_wallet->error() : qEnvironmentVariable("PLATINUM_WALLET_ERROR");
        if (!reason.isEmpty()) appearanceError += " " + reason;
        emit errorOccurred(appearanceError);
        return false;
    }
    // Let Firefox draw its tab strip, without GTK's separate title row, inside our
    // outer frame. The owned profile also hides its duplicate caption buttons.
    if (firefoxLaunch(program))
        environment.insert("MOZ_GTK_TITLEBAR_DECORATION", "client");
    configureKnownEmulatorEnvironment(program, environment);
    for (const auto &subdirectory : {"config", "cache", "data"})
        if (!QDir().mkpath(m_profile + "/" + subdirectory))
        {
            emit errorOccurred("The session's application profile could not be created.");
            return false;
        }
    process.setProcessEnvironment(environment);
    process.setWorkingDirectory(directory.isEmpty() ? m_workspace : directory);
    process.setProgram(program);
    process.setArguments(arguments);
    if (!process.startDetached())
    {
        emit errorOccurred(program + ": " + process.errorString());
        return false;
    }
    return true;
}
void NativeManager::launchFinder(QString path)
{
    if (path.isEmpty())
        path = m_workspace;
    QStringList arguments{"--root", m_workspace, "--profile", m_profile, "--open", path};
    if (!FinderPlaces::trash.isEmpty())
        arguments << "--trash" << FinderPlaces::trash;
    for (const auto &volume : FinderPlaces::volumes)
        arguments << "--volume" << volume;
    launch(QCoreApplication::applicationDirPath() + "/frankenstein-finder", arguments);
}
void NativeManager::showFinder()
{
    if (shellModal())
        return;
    if (m_state.applications.at(m_desktopApplication).windows_back_to_front.empty())
        launchFinder();
    else
        dispatch(ActivateApplication{m_desktopApplication});
}
void NativeManager::launchTerminal()
{
    auto command = terminalCommand(qEnvironmentVariable("PATH", "/usr/local/bin:/usr/bin"),
                                   qEnvironmentVariable("TERMINAL"));
    if (command.isEmpty())
    {
        emit errorOccurred("No terminal application is installed. Install one, such as Konsole or foot.");
        return;
    }
    const auto program = command.takeFirst();
    launch(program, command);
}
void NativeManager::launchPuzzle()
{
    launch(QCoreApplication::applicationDirPath() + "/frankenstein-puzzle", {});
}
void NativeManager::launchTrash()
{
    const auto path = FinderPlaces::trashFor(m_profile) + "/files";
    QDir().mkpath(path);
    launchFinder(path);
}
bool NativeManager::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_view && (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease))
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (event->type() == QEvent::KeyRelease && key->key() == m_swallowedMenuKey)
        {
            if (!key->isAutoRepeat())
                m_swallowedMenuKey = 0;
            return true;
        }
        const auto code = key->key();
        const bool modifier = code == Qt::Key_Shift || code == Qt::Key_Control || code == Qt::Key_Alt ||
                              code == Qt::Key_Meta || code == Qt::Key_Super_L || code == Qt::Key_Super_R ||
                              code == Qt::Key_AltGr || code == Qt::Key_CapsLock;
        if (event->type() == QEvent::KeyPress && m_shellMenuOpen && !modifier)
        {
            // As in 9.2.2: the key closes the menu and goes nowhere else.
            m_swallowedMenuKey = code;
            emit shellMenuKeyPressed();
            return true;
        }
    }
    if (object == m_view && m_nativeDragging && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)
    {
        m_compositor->defaultSeat()->drag()->cancelDrag();
        m_nativeDragging = false;
        m_nativeDragOnDesktop = false;
        m_pendingDesktopDrop = false;
        ++m_dropGeneration;
        m_desktopItems->clearExternalDrag();
        emit nativeDragChanged();
        queueClientFlush();
        // Still forward Escape to the owning client so its QDrag loop exits.
    }
    if (object == m_view && m_nativeDragging && m_compositor &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonRelease))
    {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const bool desktop = routeNativeDrag(mouse->position(), mouse->modifiers());
        if (event->type() == QEvent::MouseMove)
            return true;
        if (mouse->button() == Qt::LeftButton)
        {
            m_pendingDesktopDrop = desktop;
            m_pendingDropPosition = mouse->position().toPoint();
            m_pendingDropCopy = m_nativeDragCopy;
            m_pendingDropSourcePid = liveSurfaceClientPid(m_dragOrigin);
            const auto generation = ++m_dropGeneration;
            if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST")=="1")
                qInfo("Drop release generation=%llu desktop=%d pointer=%d origin=%d", generation,
                      desktop, m_compositor->defaultSeat()->pointer()->isButtonPressed(),
                      int(m_pendingDropSourcePid));
            QTimer::singleShot(0, this,
                               [this, generation]
                               {
                                   if (m_dropGeneration != generation)
                                       return;
                                   m_nativeDragging = false;
                                   m_dragOrigin.clear();
                                   m_desktopItems->clearExternalDrag();
                                   emit nativeDragChanged();
                               });
            QTimer::singleShot(5000, this,
                               [this, generation]
                               {
                                   if (m_dropGeneration == generation && m_pendingDesktopDrop)
                                   {
                                       m_pendingDesktopDrop = false;
                                       auto *seat = m_compositor->defaultSeat();
                                       seat->drag()->cancelDrag();
                                       seat->drag()->drop();
                                       if (seat->pointer()->isButtonPressed())
                                           seat->sendMouseReleaseEvent(Qt::LeftButton);
                                       queueClientFlush();
                                       emit errorOccurred(
                                           "The desktop did not receive the file drop. The pending "
                                           "transfer was cancelled.");
                                   }
                               });
        }
        // Let the source surface item end its grab and dispatch wl_data_device.drop.
    }
    if (object == m_view &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease ||
         event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::MouseMove || event->type() == QEvent::Wheel))
        queueClientFlush();
    if (object == m_view && event->type() == QEvent::DevicePixelRatioChange)
        QTimer::singleShot(0, this, &NativeManager::hostScaleChanged);
    if (object == m_view && event->type() == QEvent::Close && !m_allowExit)
    {
        quitSession();
        return true;
    }
    return QAbstractListModel::eventFilter(object, event);
}
void NativeManager::quitSession()
{
    if (shellModal() || m_desktopItems->busy())
    {
        emit errorOccurred(
            shellModal()
                ? "Finish or cancel the open dialog before ending the session."
                : "Wait for the desktop file operation to finish before ending the session.");
        return;
    }
    if (m_quitting)
        return;
    m_quitting = true;
    if (!m_compositor || m_windows.isEmpty())
    {
        endSession();
        return;
    }
    // Requests only: the application owns its save/cancel decision. Never kill
    // a client to end this nested session. The parent desktop is not involved.
    for (auto *window : QList(m_windows))
        if (window->top)
            window->top->sendClose();
    queueClientFlush();
    auto *timer = new QTimer(this);
    timer->setInterval(150);
    connect(timer, &QTimer::timeout, this,
            [this, timer]
            {
                if (m_windows.isEmpty() && !m_desktopItems->busy() && !shellModal())
                {
                    timer->stop();
                    timer->deleteLater();
                    endSession();
                }
            });
    timer->start();
    QTimer::singleShot(2500, this,
                       [this, timer]
                       {
                           if (m_quitting && !m_allowExit && !m_power->busy())
                           {
                               timer->stop();
                               timer->deleteLater();
                               m_quitting = false;
                               m_pendingPower.reset();
                               emit errorOccurred(
                                   "The session remains open. Save or close application windows, "
                                   "then try again.");
                           }
                       });
}
void NativeManager::endSession()
{
    // Restart and Shut Down leave only once logind has accepted; a refusal keeps the session.
    if (m_pendingPower)
    {
        if (!m_power->request(*m_pendingPower))
        {
            m_pendingPower.reset();
            m_quitting = false;
            emit errorOccurred("The computer cannot be restarted or shut down from here.");
        }
        return;
    }
    m_allowExit = true;
    QCoreApplication::quit();
}
bool NativeManager::powerAvailable() const
{
    return m_power->available() && m_sessionControl->available();
}
void NativeManager::sleepComputer()
{
    if (!powerAvailable() || shellModal() || m_quitting || m_power->busy())
        return;
    m_pendingPower = PowerControl::Action::Sleep;
    m_power->request(PowerControl::Action::Sleep);
}
void NativeManager::restartComputer()
{
    if (!powerAvailable() || m_quitting || m_power->busy())
        return;
    m_pendingPower = PowerControl::Action::Restart;
    quitSession();
    if (!m_quitting)
        m_pendingPower.reset();
}
void NativeManager::shutDownComputer()
{
    if (!powerAvailable() || m_quitting || m_power->busy())
        return;
    m_pendingPower = PowerControl::Action::ShutDown;
    quitSession();
    if (!m_quitting)
        m_pendingPower.reset();
}
void NativeManager::rememberRecentApplication(const QString &id)
{
    auto name = m_catalog->applicationName(id);
    if (name.isEmpty())
        name = QString(id).remove(".desktop").section('.', -1);
    m_recentApplications.erase(
        std::remove_if(m_recentApplications.begin(), m_recentApplications.end(),
                       [&](const QVariant &entry) { return entry.toMap().value("id") == id; }),
        m_recentApplications.end());
    m_recentApplications.prepend(QVariantMap{{"id", id}, {"name", name}});
    while (m_recentApplications.size() > 10)
        m_recentApplications.removeLast();
    QJsonArray array;
    for (const auto &entry : m_recentApplications)
        array.push_back(QJsonObject::fromVariantMap(entry.toMap()));
    QSaveFile file(m_profile + "/recent-applications.json");
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
        file.commit();
    }
    emit recentApplicationsChanged();
}
QVariantList NativeManager::windowMenu() const
{
    QVariantList result;
    if (!m_state.active.application)
        return result;
    QList<ManagedWindow *> windows;
    for (auto *window : m_windows)
    {
        const auto state = m_state.windows.find(window->identity);
        if (state != m_state.windows.end() && state->second.application == *m_state.active.application)
            windows.push_back(window);
    }
    std::sort(windows.begin(), windows.end(), [](auto *a, auto *b)
              { return QString::localeAwareCompare(a->title(), b->title()) < 0; });
    for (auto *window : windows)
        result.push_back(QVariantMap{{"id", window->id()},
                                     {"title", window->title().isEmpty() ? "Untitled" : window->title()},
                                     {"active", window->active()},
                                     {"collapsed", window->collapsed()}});
    return result;
}
QString NativeManager::aboutThisComputer() const
{
    QString memory = "Unknown";
    QFile meminfo("/proc/meminfo");
    // /proc files report a size of 0, so QFile::atEnd() is true before anything is read: read it whole.
    if (meminfo.open(QIODevice::ReadOnly))
        for (const auto &raw : meminfo.read(64 * 1024).split('\n'))
        {
            const auto line = raw.simplified();
            if (line.startsWith("MemTotal:"))
            {
                if (const auto kilobytes = line.split(' ').value(1).toLongLong(); kilobytes > 0)
                    memory = QLocale::system().formattedDataSize(kilobytes * 1024, 1,
                                                                 QLocale::DataSizeTraditionalFormat);
                break;
            }
        }
    return QString("Platinum Reality\n\nSystem: %1\nKernel: %2 %3\nBuilt-in Memory: %4\n\n"
                   "An original desktop inspired by Mac OS 9. No Apple software or artwork is used.")
        .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion(), memory);
}
QVariantMap NativeManager::snapshot() const
{
    QVariantList windows;
    for (auto *w : m_windows)
    {
        if (!m_state.windows.contains(w->identity))
            continue;
        windows.push_back(
            QVariantMap{{"id", w->id()},
                        {"surfaceId", w->xdg && w->xdg->surface() && w->xdg->surface()->resource()
                                          ? wl_resource_get_id(w->xdg->surface()->resource())
                                          : 0},
                        {"title", w->title()},
                        {"x", w->x()},
                        {"y", w->y()},
                        {"width", w->width()},
                        {"height", w->height()},
                        {"modal", w->modal()},
                        {"canResize", w->canResize()},
                        {"canZoom", w->canZoom()},
                        {"hasCloseBox", w->hasCloseBox()},
                        {"serverDecorated", w->serverDecorated()},
                        {"fullScreen", w->fullScreen},
                        {"classicBrowserFrame", w->classicBrowserFrame},
                        {"viaX11Bridge", w->viaX11Bridge},
                        {"collapsed", w->collapsed()},
                        {"shadeAnimating", w->shadeInMotion},
                        {"visible", w->shown()},
                        {"active", w->active()}});
    }
    return {{"revision", qulonglong(m_state.revision)},
            {"activeApplication", activeApplication()},
            {"desktopActive", desktopActive()},
            {"shellModal", shellModal()},
            {"desktopBusy", m_desktopItems->busy()},
            {"dropReceiverReady", m_dropSurface && m_dropSurface->hasContent()},
            {"nativeDragging", m_nativeDragging},
            {"menuTracking", bool(m_state.menu)},
            {"wallet", QVariantMap{{"requested", m_walletRequested},
                                   {"pending", m_wallet && m_wallet->pending()},
                                   {"ready", m_wallet && m_wallet->ready()},
                                   {"error", m_wallet ? m_wallet->error() :
                                        m_walletRequested ? qEnvironmentVariable("PLATINUM_WALLET_ERROR") : QString()}}},
            {"x11", QVariantMap{{"state", m_x11 ? m_x11->state() : "disabled"},
                                 {"display", m_x11 ? m_x11->display() : QString()}}},
            {"windows", windows}};
}
