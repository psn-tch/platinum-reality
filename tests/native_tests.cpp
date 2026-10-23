#include "applications/application_catalog.hpp"
#include "applications/application_appearance.hpp"
#include "applications/launch_environment.hpp"
#include "applications/emulator_launch.hpp"
#include "desktop/appearance_settings.hpp"
#include "desktop/desktop_items.hpp"
#include "desktop/manager.hpp"
#include "desktop/alert_sounds.hpp"
#include "desktop/attention.hpp"
#include "desktop/clipboard.hpp"
#include "desktop/menu_bridge.hpp"
#include "desktop/menu_broker.hpp"
#include "desktop/output_geometry.hpp"
#include "desktop/x11_bridge.hpp"
#include "desktop/surface_client.hpp"
#include "desktop/screenshot_service.hpp"
#include "desktop/view_lifecycle.hpp"
#include "desktop/graphics_integration.hpp"
#include "desktop/host_pointer.hpp"
#ifdef PLATINUM_MULTI_DISPLAY_EXPERIMENT
#include "desktop/mirror_cursor.hpp"
#include "desktop/mirror_touch.hpp"
#include <QLineF>
#include <QtGui/private/qeventpoint_p.h>
#include <vector>
#endif
#include "native_keys.hpp"
#include <QBuffer>
#include <QClipboard>
#include <QMimeData>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQuickItem>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWaylandPointer>
#include <QWaylandClient>
#include <QWaylandQuickItem>
#include <QWaylandQuickShellSurfaceItem>
#include <QWaylandQuickOutput>
#include <QWaylandSeat>
#include <QWaylandSurfaceGrabber>
#include <QWaylandXdgDecorationManagerV1>
#include <QtTest>
#include <memory>
#include <atomic>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <wayland-server-core.h>

namespace
{
// Compositor focus is assigned before the external Qt client's queued window
// activation reaches its text widget. Like qWaitForWindowActive in same-process
// tests, use an explicit client acknowledgement before injecting ordinary text.
bool probeAcceptsTextInput(QDBusInterface &api)
{
    QDBusPendingReply<QString> reply = api.asyncCall("InputState");
    if (!QTest::qWaitFor([&] { return reply.isFinished(); }, 2000) || reply.isError())
        return false;
    const auto state = QJsonDocument::fromJson(reply.value().toUtf8()).object();
    return state.value("activeWindow").toBool() && state.value("textFocus").toBool();
}
#ifdef PLATINUM_MULTI_DISPLAY_EXPERIMENT
class ObservedHostPointer final : public QObject
{
  public:
    std::optional<QPointF> position;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseMove)
            position = static_cast<QMouseEvent *>(event)->position();
        return false;
    }
};
class ObservedMirrorTouch final : public QObject
{
  public:
    bool consume = false;
    std::vector<std::unique_ptr<QTouchEvent>> events;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate ||
            event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel)
        {
            events.emplace_back(static_cast<QTouchEvent *>(event)->clone());
            if (consume) { event->accept(); return true; }
        }
        return false;
    }
};
// Experiment only: tests fit-to-screen rendering and absolute mouse routing.
// Wheel, drag-and-drop, relative capture and cursor mirroring are NOT supported
// by this proof and must be designed before exposing a production Mirror mode.
class ExperimentalMirrorInput final : public QObject
{
  public:
    QQuickView *view=nullptr;
    std::optional<DisplayLayout> layout;
    QRectF screen{1120,0,960,600};
    QRectF picture;
    bool forwarding=false;
    bool eventFilter(QObject *,QEvent *event) override
    {
        if(forwarding || (event->type()!=QEvent::MouseMove && event->type()!=QEvent::MouseButtonPress
                           && event->type()!=QEvent::MouseButtonRelease)) return false;
        auto *mouse=static_cast<QMouseEvent *>(event);
        if(!screen.contains(mouse->position())) return false;
        if(!picture.contains(mouse->position())) return true; // Letterboxing is not desktop content.
        const auto mapped=layout->hostToDesktop(mouse->position());
        if(!mapped) return true;
        QMouseEvent translated(mouse->type(),*mapped,*mapped,mouse->globalPosition(),mouse->button(),
                               mouse->buttons(),mouse->modifiers(),mouse->pointingDevice());
        translated.setTimestamp(mouse->timestamp());
        forwarding=true;
        QCoreApplication::sendEvent(view,&translated);
        forwarding=false;
        return true;
    }
};
#endif
class KeyCounter : public QObject
{
  public:
    int presses = 0;
    int mousePresses = 0, doubleClicks = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress)
            ++presses;
        if (event->type() == QEvent::MouseButtonPress) ++mousePresses;
        if (event->type() == QEvent::MouseButtonDblClick) ++doubleClicks;
        return false;
    }
};
// Each scenario gets its own compositor, application profile and fixture files.
// The surrounding launcher supplies the private test-only bus and runtime.
class NativeScene
{
  public:
    QTemporaryDir files;
    PlatinumCompositor compositor; // As the desktop: it reports kept clipboards.
    QWaylandXdgShell shell{&compositor};
    QWaylandXdgDecorationManagerV1 decoration;
    std::unique_ptr<QWaylandQuickOutput> output;
    std::unique_ptr<DesktopOutputGeometry> outputGeometry;
    std::unique_ptr<NativeMenuBridge> bridge;
    X11Bridge x11;
    DesktopClientRetirement retireClients{&compositor};
    X11BridgeRetirement retireX11{&x11};
    QQuickView view;
    std::unique_ptr<NativeManager> manager;
    QTimer clientFlush;
    NativeScene()
    {
        view.resize(1120, 760);
        // Match the production view when the host compositor resizes this window.
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        // A failed third-party client must not reconnect to the next scenario.
        static uint nextSocket = 0;
        compositor.setSocketName("frankenstein-qualification-" + QByteArray::number(++nextSocket));
        decoration.setExtensionContainer(&compositor);
        decoration.setPreferredMode(QWaylandXdgToplevel::ServerSideDecoration);
        // Recorded fallback check only: a session without GPU client buffers.
        if (qEnvironmentVariable("FRANKENSTEIN_TEST_NO_GPU_BUFFERS") == "1")
            qputenv("QT_WAYLAND_HARDWARE_INTEGRATION", "none");
        configureCompositorGraphics(); // Same startup selection as the desktop.
        compositor.create();
        shell.initialize();
        decoration.initialize();
        // Production output description. The baseline switch exists only for the
        // recorded before/after layout comparison; it is never a gate setting.
        if (qEnvironmentVariable("FRANKENSTEIN_OUTPUT_BASELINE") != "1")
            outputGeometry = std::make_unique<DesktopOutputGeometry>(&compositor, &view);
        else {
            output = std::make_unique<QWaylandQuickOutput>(&compositor, &view);
            output->setSizeFollowsWindow(true); // Recorded baseline only.
        }
        bridge = std::make_unique<NativeMenuBridge>(&compositor);
        manager = std::make_unique<NativeManager>(&compositor, &view, files.path(),
                                                  files.path() + "/profile", QString(),
                                                  QStringList{files.path() + "/applications"});
        QObject::connect(manager.get(), &NativeManager::errorOccurred, manager.get(),
                         [](const QString &message)
                         { qWarning().noquote() << "Shell:" << message; });
        manager->attachShell(&shell);
#ifdef PLATINUM_MULTI_DISPLAY_EXPERIMENT
        QObject::connect(manager.get(), &NativeManager::displayLayoutChanged, outputGeometry.get(), [this] {
            if(outputGeometry) outputGeometry->setDisplayLayout(manager->effectiveDisplayLayout());
        });
#endif
        manager->setX11Bridge(&x11);
        x11.start(compositor.socketName());
        QObject::connect(bridge.get(), &NativeMenuBridge::endpointChanged, manager.get(),
                         &NativeManager::setMenuEndpoint);
        view.rootContext()->setContextProperty("manager", manager.get());
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        // The production event loop flushes queued client events whenever it is
        // about to block. QTest::qWait and qWaitFor poll without blocking, so a
        // synthetic click could otherwise stay in the server buffer until the
        // client happened to send a request: the intermittent lost first click.
        clientFlush.setInterval(4);
        QObject::connect(&clientFlush, &QTimer::timeout, &clientFlush,
                         [this] { wl_display_flush_clients(compositor.display()); });
        clientFlush.start();
    }
    ~NativeScene()
    {
        releaseDesktopView(view);
    }
    bool show()
    {
        view.show();
        return view.status() != QQuickView::Error && QTest::qWaitForWindowExposed(&view);
    }
    QProcessEnvironment childEnvironment() const
    {
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("FRANKENSTEIN_ISOLATED", "1");
        env.insert("WAYLAND_DISPLAY", QString::fromUtf8(compositor.socketName()));
        env.insert("QT_QPA_PLATFORM", "wayland");
        env.insert("QT_QPA_PLATFORMTHEME", "kde");
        env.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
        env.insert("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
        env.insert("XDG_CONFIG_HOME", files.path() + "/config");
        env.insert("XDG_DATA_HOME", files.path() + "/data");
        env.insert("XDG_CACHE_HOME", files.path() + "/cache");
        env.remove("DISPLAY");
        env.remove("XAUTHORITY");
        env.remove("WAYLAND_SOCKET");
        x11.applyTo(env);
        return env;
    }
};
QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
QString externalApplicationPlugins()
{
    // An ordinary external browser cannot load an ASan-instrumented Qt plugin.
    // Instrumented gates explicitly supply the matching normal-build style;
    // the owned instrumented Qt probes retain their own instrumented plugin.
    return qEnvironmentVariable("FRANKENSTEIN_EXTERNAL_STYLE_DIRECTORY",
                                 QCoreApplication::applicationDirPath()+"/plugins");
}
// Fixture browsers stay silent: no user audio sockets, and libpulse keeps its
// runtime state in the disposable scene instead of the private XDG runtime.
void silenceFixtureAudio(QProcessEnvironment &env, const QString &root)
{
    env.insert("PIPEWIRE_RUNTIME_DIR", root + "/no-audio");
    env.insert("PULSE_SERVER", "unix:" + root + "/no-audio/native");
    env.insert("PULSE_RUNTIME_PATH", root + "/no-audio");
}
// Kernel wait channel of each thread in a hung fixture browser: names only.
void printThreadWaits(qint64 pid)
{
    const auto tasks = QDir(QString("/proc/%1/task").arg(pid)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &task : tasks.mid(0, 80))
    {
        const auto base = QString("/proc/%1/task/%2/").arg(pid).arg(task);
        const auto stat = bytes(base + "stat");
        const auto state = stat.mid(stat.lastIndexOf(')') + 2, 1);
        qInfo().noquote() << "Thread" << task << bytes(base + "comm").trimmed() << state
                          << bytes(base + "wchan");
    }
    // Only when a fixture opted in to a ptracer shim (diagnostics, never a gate).
    if (!qEnvironmentVariableIsSet("FRANKENSTEIN_FIXTURE_PTRACER"))
        return;
    QProcess gdb;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("DEBUGINFOD_URLS", "");
    gdb.setProcessEnvironment(env);
    gdb.setStandardOutputFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR") + "/hung-browser-stack.txt");
    gdb.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR") + "/hung-browser-gdb-stderr.txt");
    gdb.start("gdb", {"-p", QString::number(pid), "-batch", "-nx", "-ex", "set debuginfod enabled off",
                      "-ex", "thread apply 1 bt 60", "-ex", "thread apply all bt 12"});
    // Long enough for gdb to read Firefox's libraries without debuginfod; still well inside the
    // fixture watchdog. Opt-in diagnostics only.
    if (!gdb.waitForFinished(15000)) {
        gdb.kill();
        gdb.waitForFinished(1000);
        qInfo("Owned-browser stack capture reached its fifteen-second deadline");
    }
}
// Bounded input/activation protocol trace for one scene, printed only when a
// browser scenario fails. Interface/message names, object ids, coordinates and
// xdg states only: no key codes, text, titles or URLs.
class WireTrace
{
  public:
    explicit WireTrace(QWaylandCompositor &compositor)
    {
        m_clock.start();
        m_logger = wl_display_add_protocol_logger(compositor.display(), &WireTrace::record, this);
    }
    ~WireTrace()
    {
        wl_protocol_logger_destroy(m_logger);
    }
    void mark(const QString &note)
    {
        append(QStringLiteral("-- ") + note);
    }
    void print() const
    {
        // Marks plus the most recent traffic; startup commits are not useful.
        for (qsizetype i = 0; i < m_lines.size(); ++i)
            if (i >= m_lines.size() - 160 || m_lines.at(i).startsWith("-- ") ||
                m_lines.at(i).contains("xdg_toplevel") || m_lines.at(i).contains("wl_pointer") ||
                m_lines.at(i).contains("wl_keyboard") || m_lines.at(i).contains("wl_data_"))
                qInfo().noquote() << "Wire:" << m_lines.at(i);
    }

  private:
    static void record(void *data, wl_protocol_logger_type direction,
                       const wl_protocol_logger_message *m)
    {
        auto *self = static_cast<WireTrace *>(data);
        const QByteArray cls = wl_resource_get_class(m->resource), name = m->message->name;
        const bool event = direction == WL_PROTOCOL_LOGGER_EVENT;
        if (cls == "wl_pointer" && event && name == "frame")
            return;
        static const bool frames = qEnvironmentVariableIsSet("FRANKENSTEIN_WIRE_TRACE_FRAMES");
        if (!(cls == "wl_pointer" || (cls == "wl_keyboard" && name != "key" && name != "modifiers") ||
              cls.startsWith("wl_data_") ||
              cls == "xdg_toplevel" || cls == "xdg_popup" ||
              (cls == "zwp_linux_buffer_params_v1" && name == "create_immed") ||
              (cls == "xdg_surface" && name != "set_window_geometry") ||
              (frames && (cls == "wl_callback" || (cls == "wl_display" && name == "sync") ||
                          (cls == "wl_surface" && (name == "frame" || name == "commit"))))))
            return;
        QString line = QString::number(self->m_clock.elapsed()) + " " + (event ? "event " : "request ") +
                       QString::fromLatin1(cls + "." + name) + "#" +
                       QString::number(wl_resource_get_id(m->resource));
        if (cls == "zwp_linux_buffer_params_v1" && name == "create_immed")
            line += QString(" %1x%2 format=0x%3").arg(m->arguments[1].i).arg(m->arguments[2].i).arg(m->arguments[3].u,8,16,QChar('0'));
        if (cls == "wl_pointer" && event && (name == "enter" || name == "leave") && m->arguments[1].o)
            line += " surface#" + QString::number(wl_resource_get_id(
                                      reinterpret_cast<wl_resource *>(m->arguments[1].o)));
        if (cls == "wl_keyboard" && event && (name == "enter" || name == "leave") && m->arguments[1].o)
            line += " surface#" + QString::number(wl_resource_get_id(
                                      reinterpret_cast<wl_resource *>(m->arguments[1].o)));
        // Clipboard transfers: MIME type names only, never the transferred data.
        if ((cls == "wl_data_source" || cls == "wl_data_offer") &&
            (name == "offer" || name == "receive" || name == "send") && m->arguments[0].s)
        {
            // Standard type names only; owned drag types can carry tokens.
            const QByteArray mime(m->arguments[0].s);
            static const QRegularExpression safe(
                "^(text/[a-z0-9.+-]+(;charset=[a-z0-9-]+)?|image/[a-z0-9.+-]+|UTF8_STRING|STRING|TEXT|"
                "COMPOUND_TEXT|SAVE_TARGETS|chromium/x-[a-z-]+|application/x-moz-[a-z-]+)$");
            line += " " + (safe.match(QString::fromLatin1(mime)).hasMatch() ? QString::fromLatin1(mime)
                                                                             : QStringLiteral("other"));
        }
        if (cls == "wl_pointer" && event && name == "motion")
            line += QString(" %1,%2").arg(wl_fixed_to_double(m->arguments[1].f))
                        .arg(wl_fixed_to_double(m->arguments[2].f));
        if (cls == "wl_pointer" && event && name == "button")
            line += QString(" button=%1 state=%2").arg(m->arguments[2].u).arg(m->arguments[3].u);
        if (cls == "xdg_toplevel" && event && name == "configure")
        {
            line += QString(" %1x%2 states=").arg(m->arguments[0].i).arg(m->arguments[1].i);
            if (const auto *states = m->arguments[2].a)
                for (std::size_t i = 0; i < states->size / sizeof(uint32_t); ++i)
                    line += QString::number(static_cast<const uint32_t *>(states->data)[i]) + ",";
        }
        self->append(line);
    }
    void append(const QString &line)
    {
        if (m_lines.size() < 20000)
            m_lines.append(line);
    }
    wl_protocol_logger *m_logger = nullptr;
    QElapsedTimer m_clock;
    QStringList m_lines;
};
#ifdef PLATINUM_MULTI_DISPLAY_EXPERIMENT
// Actual wl_surface enter/leave events, paired with the client-side QScreen
// observations below. Track every surface separately to catch duplicate enters
// while a window's dominant output changes without leaving either monitor.
class OutputTrace
{
  public:
    explicit OutputTrace(QWaylandCompositor &compositor)
        : logger(wl_display_add_protocol_logger(compositor.display(), &OutputTrace::record, this)) {}
    ~OutputTrace() { wl_protocol_logger_destroy(logger); }
    QSet<QString> outputs(pid_t pid) const {
        QSet<QString> result;
        for(auto it=entered.cbegin();it!=entered.cend();++it)
            if(it.key().first==pid) result.unite(it.value());
        return result;
    }
    QString lastFullscreen;
    int unbalanced=0;
  private:
    static void record(void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *m) {
        auto *self=static_cast<OutputTrace *>(data);
        const QByteArray type=wl_resource_get_class(m->resource), name=m->message->name;
        if(type=="wl_surface" && name=="destroy" && direction==WL_PROTOCOL_LOGGER_REQUEST) {
            pid_t pid=0;
            wl_client_get_credentials(wl_resource_get_client(m->resource),&pid,nullptr,nullptr);
            self->entered.remove({pid,wl_resource_get_id(m->resource)});
        }
        if(type=="xdg_toplevel" && name=="set_fullscreen" && direction==WL_PROTOCOL_LOGGER_REQUEST) {
            auto *output=m->arguments[0].o ? QWaylandOutput::fromResource(
                reinterpret_cast<wl_resource *>(m->arguments[0].o)) : nullptr;
            self->lastFullscreen=output?output->property("platinumDisplayName").toString():QString();
        }
        if(type!="wl_surface" || direction!=WL_PROTOCOL_LOGGER_EVENT || (name!="enter" && name!="leave")) return;
        auto *output=QWaylandOutput::fromResource(reinterpret_cast<wl_resource *>(m->arguments[0].o));
        if(!output) return;
        const auto connector=output->property("platinumDisplayName").toString();
        pid_t pid=0;
        wl_client_get_credentials(wl_resource_get_client(m->resource),&pid,nullptr,nullptr);
        auto &active=self->entered[{pid,wl_resource_get_id(m->resource)}];
        if(name=="enter") {
            if(active.contains(connector)) ++self->unbalanced;
            active.insert(connector);
        } else {
            if(!active.remove(connector)) ++self->unbalanced;
        }
    }
    wl_protocol_logger *logger;
    QMap<QPair<pid_t,uint32_t>,QSet<QString>> entered;
};
#endif
// A line-oriented child process: the pointer probe and the host virtual-pointer driver.
class LineProcess
{
  public:
    QProcess process;
    QStringList lines;
    qsizetype read = 0;
    LineProcess()
    {
        process.setStandardErrorFile(QProcess::nullDevice());
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, [this] {
            while (process.canReadLine())
                lines.append(QString::fromUtf8(process.readLine()).trimmed());
        });
    }
    ~LineProcess()
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(3000);
        }
    }
    // The next unread line that starts with prefix; earlier unread lines are skipped.
    QString next(const QString &prefix, int timeout = 5000)
    {
        QString found;
        (void)QTest::qWaitFor([&] {
            while (read < lines.size())
                if (lines.at(read++).startsWith(prefix))
                {
                    found = lines.at(read - 1);
                    return true;
                }
            return false;
        }, timeout);
        return found;
    }
    // Unread lines starting with prefix, without consuming them.
    QStringList pending(const QString &prefix) const
    {
        QStringList result;
        for (qsizetype i = read; i < lines.size(); ++i)
            if (lines.at(i).startsWith(prefix))
                result.append(lines.at(i));
        return result;
    }
    void skip()
    {
        read = lines.size();
    }
    bool send(const QString &command, const QString &reply)
    {
        process.write(command.toUtf8() + "\n");
        return !next(reply).isEmpty();
    }
};
// A child that only this test spawns and reaps. Its pidfd names exactly that process, so a
// signal can never reach a reused PID. Its stdin and stdout are plain pipes, so another thread
// may send it a command while this one is blocked.
class OwnedProcess
{
  public:
    QStringList lines;
    ~OwnedProcess()
    {
        stop();
    }
    bool start(const QString &program, const QProcessEnvironment &environment)
    {
        int in[2], out[2];
        if (::pipe2(in, O_CLOEXEC) != 0)
            return false;
        if (::pipe2(out, O_CLOEXEC) != 0)
        {
            ::close(in[0]);
            ::close(in[1]);
            return false;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        const auto path = program.toLocal8Bit();
        QByteArrayList variables;
        for (const auto &variable : environment.toStringList())
            variables.push_back(variable.toLocal8Bit());
        std::vector<char *> envp;
        for (auto &variable : variables)
            envp.push_back(variable.data());
        envp.push_back(nullptr);
        char *argv[] = {const_cast<char *>(path.constData()), nullptr};
        const int spawned = posix_spawn(&m_pid, path.constData(), &actions, nullptr, argv, envp.data());
        posix_spawn_file_actions_destroy(&actions);
        ::close(in[0]);
        ::close(out[1]);
        m_in = in[1];
        m_out = out[0];
        ::fcntl(m_out, F_SETFL, O_NONBLOCK);
        if (spawned != 0)
            return false;
        // Nobody has reaped it yet, so the PID still names this child.
        m_pidfd = int(::syscall(SYS_pidfd_open, m_pid, 0));
        return m_pidfd >= 0;
    }
    int input() const
    {
        return m_in;
    }
    bool send(const QString &command)
    {
        const auto line = command.toUtf8() + '\n';
        return m_in >= 0 && ::write(m_in, line.constData(), size_t(line.size())) == line.size();
    }
    // The next unread line that starts with prefix; earlier unread lines are skipped.
    QString next(const QString &prefix, int timeout = 5000)
    {
        QString found;
        (void)QTest::qWaitFor([&] {
            pump();
            while (m_read < lines.size())
                if (lines.at(m_read++).startsWith(prefix))
                {
                    found = lines.at(m_read - 1);
                    return true;
                }
            return false;
        }, timeout);
        return found;
    }
    // The latest line, read or not, that starts with prefix, once there is one.
    QString latest(const QString &prefix, int timeout = 5000)
    {
        QString found;
        (void)QTest::qWaitFor([&] {
            pump();
            for (auto line = lines.crbegin(); line != lines.crend(); ++line)
                if (line->startsWith(prefix))
                {
                    found = *line;
                    return true;
                }
            return false;
        }, timeout);
        return found;
    }
    void stop()
    {
        if (m_pidfd >= 0)
        {
            ::syscall(SYS_pidfd_send_signal, m_pidfd, SIGKILL, nullptr, 0);
            siginfo_t info{};
            ::waitid(idtype_t(P_PIDFD), id_t(m_pidfd), &info, WEXITED);
            ::close(m_pidfd);
            m_pidfd = -1;
        }
        for (int *fd : {&m_in, &m_out})
            if (*fd >= 0)
            {
                ::close(*fd);
                *fd = -1;
            }
    }

  private:
    void pump()
    {
        char chunk[4096];
        ssize_t count;
        while (m_out >= 0 && (count = ::read(m_out, chunk, sizeof chunk)) > 0)
            m_buffer.append(chunk, count);
        for (qsizetype end; (end = m_buffer.indexOf('\n')) >= 0; m_buffer.remove(0, end + 1))
            lines.append(QString::fromUtf8(m_buffer.left(end)));
    }
    pid_t m_pid = -1;
    int m_pidfd = -1, m_in = -1, m_out = -1;
    qsizetype m_read = 0;
    QByteArray m_buffer;
};
// Watches this thread, which is the compositor's, from another. Whenever it has not run for 2 s,
// the watcher sends "release" to the paste probe itself, which then drains its stalled pipes. A
// compositor stuck writing into one therefore fails the test, rather than hanging it.
class Heartbeat
{
  public:
    explicit Heartbeat(int probeInput) : m_input(probeInput)
    {
        m_beat = now();
        QObject::connect(&m_timer, &QTimer::timeout, [this] { m_beat = now(); });
        m_timer.start(10);
        m_watcher = std::thread([this] {
            qint64 released = 0;
            while (!m_stop)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                const auto at = now(), gap = at - m_beat.load();
                m_worst = std::max(m_worst.load(), gap);
                if (gap > 2000 && at - released > 2000)
                {
                    released = at;
                    ++m_releases;
                    (void)!::write(m_input, "release\n", 8);
                }
            }
        });
    }
    ~Heartbeat()
    {
        m_stop = true;
        m_watcher.join();
    }
    // The longest the compositor's thread went without running, in milliseconds.
    qint64 worst() const
    {
        return m_worst;
    }
    int releases() const
    {
        return m_releases;
    }

  private:
    static qint64 now()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    int m_input;
    QTimer m_timer;
    std::atomic<qint64> m_beat{0}, m_worst{0};
    std::atomic<int> m_releases{0};
    std::atomic<bool> m_stop{false};
    std::thread m_watcher;
};
// Records org.freedesktop.Notifications signals as a client sees them on the session bus.
class NotificationSignals final : public QObject
{
    Q_OBJECT
  public:
    QList<QPair<uint, uint>> closed;
    QList<QPair<uint, QString>> actions;
  public slots:
    void notificationClosed(uint id, uint reason)
    {
        closed.append({id, reason});
    }
    void actionInvoked(uint id, const QString &key)
    {
        actions.append({id, key});
        order.append("action");
    }
    void activationToken(uint id, const QString &token)
    {
        tokens.append({id, token});
        order.append("token");
    }

  public:
    QList<QPair<uint, QString>> tokens;
    QStringList order;
};
} // namespace

class NativeTests : public QObject
{
    Q_OBJECT
    QVariantMap first(const NativeManager &manager, const QString &title)
    {
        for (const auto &value : manager.snapshot().value("windows").toList())
            if (value.toMap().value("title") == title)
                return value.toMap();
        return {};
    }
    // The compositor owns a 4 MiB clipboard of 'p': the application that copied it has quit.
    void retainLargeClipboard(NativeScene &scene)
    {
        auto &manager = *scene.manager;
        QProcess source;
        source.setProcessEnvironment(scene.childEnvironment());
        source.start(QCoreApplication::applicationDirPath() + "/native-probe", {"BigCopy"});
        const auto stop = qScopeGuard([&] {
            if (source.state() != QProcess::NotRunning)
            {
                source.kill();
                source.waitForFinished(3000);
            }
        });
        QTRY_VERIFY(!first(manager, "Native Probe BigCopy").isEmpty());
        QDBusInterface api("org.frankenstein.NativeProbeBigCopy", "/Probe", "org.frankenstein.NativeProbe");
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api), 3000);
        api.call("SetClipboard", QString(4 * 1024 * 1024, QChar('p')));
        auto *clipboard = qobject_cast<Clipboard *>(manager.clipboard());
        QVERIFY(clipboard);
        QTRY_VERIFY_WITH_TIMEOUT(clipboard->kind() == "text" && clipboard->text().startsWith("pppp"), 10000);
        api.call("Finish");
        QTRY_COMPARE(source.state(), QProcess::NotRunning);
    }
    // Starts tests/paste_probe.c and waits until it has the keyboard.
    bool startPasteProbe(OwnedProcess &probe, NativeScene &scene)
    {
        return probe.start(QCoreApplication::applicationDirPath() + "/paste-probe", scene.childEnvironment()) &&
               !probe.latest("ready", 10000).isEmpty() && !probe.latest("focus", 5000).isEmpty();
    }
    // The number of the clipboard offer the probe was last given, once it is above after and no
    // newer one has followed for 300 ms (one shell copy can be offered more than once).
    int pasteOffer(OwnedProcess &probe, int after = -1)
    {
        const auto latest = [&] {
            bool ok = false;
            const int offer = probe.latest("selection ", 0).mid(10).toInt(&ok);
            return ok ? offer : -1;
        };
        if (!QTest::qWaitFor([&] { return latest() > after; }, 5000))
            return -1;
        for (int offer = latest();;)
        {
            QTest::qWait(300);
            if (latest() == offer)
                return offer;
            offer = latest();
        }
    }
    static int threadCount()
    {
        return int(QDir("/proc/self/task").entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
    }
    int command(const QVariantList &entries, const QString &label)
    {
        for (const auto &value : entries)
        {
            const auto item = value.toMap();
            if (item.value("label").toString() == label)
                return item.value("id").toInt();
            int child = command(item.value("children").toList(), label);
            if (child >= 0)
                return child;
        }
        return -1;
    }
    bool capture(QQuickView &view, const QString &name)
    {
        const auto evidence = qEnvironmentVariable(
            "FRANKENSTEIN_EVIDENCE_DIR", QCoreApplication::applicationDirPath() + "/artifacts");
        QDir().mkpath(evidence);
        return view.grabWindow().save(evidence + "/" + name);
    }
    QQuickItem *visualItem(QQuickItem *root, const QString &name)
    {
        if (root->objectName() == name)
            return root;
        for (auto *child : root->childItems())
            if (auto *found = visualItem(child, name))
                return found;
        return nullptr;
    }
    void surfaceGeometry(QQuickView &view, QQuickItem *root)
    {
        // Bounded numeric diagnostics only: no browser argv, URLs or titles.
        qInfo() << "Host logical size:" << view.size() << "DPR:" << view.devicePixelRatio()
                << "root size:" << (view.rootObject() ? view.rootObject()->size() : QSizeF());
        int remaining = 24;
        const auto visit = [&](auto &&self, QQuickItem *parent) -> void
        {
            if (!parent || remaining <= 0)
                return;
            if (auto *item = qobject_cast<QWaylandQuickItem *>(parent))
            {
                --remaining;
                qInfo() << "Surface local bounds:" << item->boundingRect()
                        << "scene bounds:" << item->mapRectToScene(item->boundingRect())
                        << "position:" << item->position() << "scale:" << item->scale()
                        << "visible:" << item->isVisible()
                        << "destination:" << (item->surface() ? item->surface()->destinationSize()
                                                             : QSize())
                        << "output scale:" << (item->output() ? item->output()->scaleFactor() : 0);
            }
            for (auto *child : parent->childItems())
                self(self, child);
        };
        visit(visit, root);
    }
    bool clickDesktopCommand(NativeScene &scene, const QString &action)
    {
        auto *bar = visualItem(scene.view.contentItem(), "bar-entry-1");
        if (!scene.manager->desktopActive() || !bar)
            return false;
        bar->parentItem()->ensurePolished();
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          bar->mapToScene(QPointF(bar->width() / 2, 13)).toPoint());
        auto *command = visualItem(scene.view.contentItem(), "desktop-command-" + action);
        if (!command || !command->isVisible() || !command->isEnabled())
            return false;
        command->parentItem()->ensurePolished(); // Layout before pointer hit-testing.
        if (command->width() <= 0 || command->height() <= 0)
            return false;
        QTest::mouseClick(
            &scene.view, Qt::LeftButton, {},
            command->mapToScene(QPointF(command->width() / 2, command->height() / 2)).toPoint());
        return true;
    }
    // ChatGPT-style saves: a fetched Blob through a temporary download link, a
    // server attachment and, in Chromium, the File System Access save picker.
    // Download handling stays at production defaults (no download preferences,
    // no user-dirs.dirs), so files land in $HOME/Downloads of a disposable home.
    void scriptGeneratedSaves(bool chromium)
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        const auto profile=scene.files.path()+"/profile";
        const auto home=scene.files.path()+"/home";
        const auto downloads=home+"/Downloads";
        QVERIFY(QDir().mkpath(downloads));
        if (!chromium) {
            QVERIFY(QDir().mkpath(profile+"/browsers/firefox"));
            QFile prefs(profile+"/browsers/firefox/prefs.js");
            QVERIFY(prefs.open(QIODevice::WriteOnly));
            prefs.write("user_pref(\"browser.aboutwelcome.enabled\",false);\n"
                        "user_pref(\"browser.shell.checkDefaultBrowser\",false);\n"
                        "user_pref(\"browser.startup.homepage_override.mstone\",\"ignore\");\n"
                        "user_pref(\"extensions.enabledScopes\",1);\n"
                        "user_pref(\"extensions.autoDisableScopes\",15);\n"
                        "user_pref(\"datareporting.policy.firstRunURL\",\"\");\n");
            prefs.close();
        }
        QByteArray payload("Platinum Reality script-generated save\n");
        for (int value=0; value<256; ++value) payload.append(char(value));
        const QByteArray report("name,value\nplatinum,9.2.2\n");
        const QByteArray page=
            "<!doctype html><title>Loading Script Fixture</title>"
            "<button id=go style='position:fixed;inset:0;font-size:32px'>Save fixture</button><script>"
            "let step=0;go.onclick=async()=>{try{step++;"
            "if(step==1){document.title='Blob fetch started';"
            "const url=URL.createObjectURL(await (await fetch('/payload.bin')).blob());"
            "document.title='Blob payload ready';"
            "const link=document.createElement('a');link.href=url;link.download='pdfixtureblob.bin';"
            "document.body.append(link);link.click();link.remove();"
            "setTimeout(()=>URL.revokeObjectURL(url),10000);document.title='Blob save clicked'}"
            "else if(step==2){const link=document.createElement('a');link.href='/attachment';"
            "document.body.append(link);link.click();link.remove();document.title='Attachment clicked'}"
            "else{const handle=await showSaveFilePicker({suggestedName:'pdfixturepicker.bin',startIn:'downloads'});"
            "const out=await handle.createWritable();await out.write(await (await fetch('/payload.bin')).blob());"
            "await out.close();document.title='Picker saved'}"
            "}catch(error){document.title='Fixture error '+error.name}};"
            "let loaded=false;const sized=()=>requestAnimationFrame(()=>{"
            "if(loaded)document.title='Script fixture ready '+innerWidth});"
            "addEventListener('load',()=>{loaded=true;sized()});addEventListener('resize',sized)"
            "</script>";
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket=server.nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    auto request=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",request);
                    if (!request.contains("\r\n\r\n")) return;
                    QByteArray status="200 OK", type="text/html", extra, body=page;
                    if (request.startsWith("GET /payload.bin ")) {type="application/octet-stream";body=payload;}
                    else if (request.startsWith("GET /attachment ")) {
                        type="text/csv";body=report;
                        extra="Content-Disposition: attachment; filename=\"pdfixturereport.csv\"\r\n";
                    } else if (!request.startsWith("GET / ")) {status="404 Not Found";type="text/plain";body={};}
                    socket->write("HTTP/1.1 "+status+"\r\nContent-Type: "+type+"\r\n"+extra+"Content-Length: "+
                                  QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
            }
        });
        frankenstein::applications::LaunchPlan plan;
        plan.program=chromium ? "/usr/bin/chromium" : "/usr/lib/firefox/firefox";
        const auto url=QString("http://127.0.0.1:%1/").arg(server.serverPort());
        // Chromium's flags only suppress first-run UI in the disposable profile.
        plan.arguments=chromium ? QStringList{"--no-first-run","--no-default-browser-check","--new-window",url}
                                : QStringList{"--new-window",url};
        QString error;
        QVERIFY2(prepareApplicationAppearance(profile,error),qPrintable(error));
        QVERIFY2(prepareStyledApplication(plan,profile,error),qPrintable(error));
        auto env=nativeApplicationEnvironment(QProcessEnvironment::systemEnvironment(),
                   QString::fromUtf8(scene.compositor.socketName()),profile,
                   externalApplicationPlugins());
        if (!X11Bridge::helperPath().isEmpty()) {
            QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
            scene.x11.applyTo(env); // Match production with both display protocols.
        }
        if (!chromium) env.insert("MOZ_GTK_TITLEBAR_DECORATION","client"); // Match the production launch path.
        if (qEnvironmentVariableIsSet("FRANKENSTEIN_FIXTURE_PTRACER")) {
            env.insert("LD_PRELOAD", qEnvironmentVariable("FRANKENSTEIN_FIXTURE_PTRACER"));
            env.insert("MOZ_CRASHREPORTER_DISABLE", "1");
        }
        env.insert("HOME",home);
        silenceFixtureAudio(env,scene.files.path());
        for (const auto &name : {"config","cache","data"}) QVERIFY(QDir().mkpath(profile+"/"+name));
        QProcess browser;
        browser.setProcessEnvironment(env);
        browser.setWorkingDirectory(home);
        browser.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+
                                     (chromium ? "/chromium-script-save-stderr.txt" : "/firefox-script-save-stderr.txt"));
        browser.setStandardOutputFile(QProcess::nullDevice());
        const auto closeBrowser=qScopeGuard([&] {
            if (browser.state()!=QProcess::NotRunning) {browser.kill();browser.waitForFinished(5000);}
        });
        WireTrace wire(scene.compositor);
        const auto failureCapture=qScopeGuard([&] {
            if(QTest::currentTestFailed()) {
                capture(scene.view,chromium ? "native-chromium-script-save-failure.png" : "native-firefox-script-save-failure.png");
                if (browser.state()==QProcess::Running) printThreadWaits(browser.processId());
                wire.print();
            }
        });
        browser.start(plan.program,plan.arguments);
        QVERIFY(browser.waitForStarted());
        qulonglong id=0;
        QVERIFY2(QTest::qWaitFor([&]{
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("title").toString().startsWith("Script fixture ready"))
                    {id=value.toMap().value("id").toULongLong();return true;}
            return false;
        },20000),"The browser did not load the script save fixture.");
        const auto title=[&] {
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("id").toULongLong()==id) return value.toMap().value("title").toString();
            return QString();
        };
        const auto reached=[&](const QString &state) {
            return QTest::qWaitFor([&]{return title().startsWith(state) || title().startsWith("Fixture error");},8000)
                   && title().startsWith(state);
        };
        auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client);
        QVERIFY2(QFileInfo(QString("/proc/%1/exe").arg(liveSurfaceClientPid(client->surface()))).canonicalFilePath()
                     .endsWith(chromium ? "/chromium" : "/firefox"),
                 "The browser unexpectedly used the X11 bridge rather than native Wayland");
        manager.moveWindow(id,130,80);
        manager.resizeWindow(id,800,500);
        const auto sizeReady=[&] {
            for(const auto &entry:manager.snapshot().value("windows").toList())
                if(entry.toMap().value("id").toULongLong()==id)
                    return entry.toMap().value("width").toInt()==800 && entry.toMap().value("height").toInt()==500;
            return false;
        };
        QTRY_VERIFY(sizeReady());
        QVERIFY2(QTest::qWaitFor([&]{return title().startsWith("Script fixture ready 800");},5000),
                 qPrintable("The script fixture did not finish layout at the requested width: "+title()));
        manager.activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        const auto click=[&] {
            wire.mark("click");
            const auto point=client->mapToScene(client->mapFromSurface(QPointF(200,300))).toPoint();
            QTest::mouseMove(&scene.view,point);
            QTest::mouseClick(&scene.view,Qt::LeftButton,{},point);
        };
        click();
        QVERIFY2(reached("Blob save clicked"),qPrintable("Unexpected page state: "+title()));
        QVERIFY2(QTest::qWaitFor([&]{return bytes(downloads+"/pdfixtureblob.bin")==payload;},8000),
                 "The browser did not save a script-generated Blob into its default download folder.");
        click();
        QVERIFY2(reached("Attachment clicked"),qPrintable("Unexpected page state: "+title()));
        QVERIFY2(QTest::qWaitFor([&]{return bytes(downloads+"/pdfixturereport.csv")==report;},8000),
                 "The browser did not save a server attachment into its default download folder.");
        if (chromium) {
            const auto chooser=[&] {
                for(const auto &value:manager.snapshot().value("windows").toList())
                    if(value.toMap().value("id").toULongLong()!=id) return value.toMap().value("id").toULongLong();
                return qulonglong(0);
            };
            click();
            QVERIFY2(QTest::qWaitFor([&]{return chooser()!=0;},8000),
                     qPrintable("showSaveFilePicker opened no chooser; page state: "+title()));
            // Chromium's GTK chooser arrives on its own Wayland connection without
            // an xdg-foreign parent. Wait for the compositor's keyboard focus.
            auto *dialog=qobject_cast<QWaylandQuickShellSurfaceItem *>(
                visualItem(scene.view.contentItem(),"client-"+QString::number(chooser())));
            QVERIFY2(dialog && QTest::qWaitFor([&]{return dialog->hasActiveFocus();},5000),
                     "The save chooser never received keyboard focus.");
            auto *dialogSurface=qobject_cast<QWaylandXdgSurface *>(dialog->shellSurface());
            QVERIFY(dialogSurface && dialogSurface->toplevel());
            // The oversized chooser is fitted to the desktop and must stay activated:
            // a fit sent before the client acknowledged activation used to drop it.
            QVERIFY2(QTest::qWaitFor([&]{
                         const auto size=dialogSurface->windowGeometry().size();
                         return size.width()<=scene.view.width()-12 && size.height()<=scene.view.height()-60
                                && dialogSurface->toplevel()->activated();},5000),
                     "The fitted save chooser was not left activated.");
            QVERIFY(capture(scene.view,"native-chromium-picker-dialog.png"));
            // Type over the selected base name; the saved file name proves the
            // keys reached Chromium's separate GTK connection.
            QVERIFY(typeAscii(&scene.view,"pdtyped"));
            // Then press Save as a person would: the header-bar button at the
            // top-right of the GTK chooser's window geometry.
            const auto geometry=dialogSurface->windowGeometry();
            const auto save=dialog->mapToScene(dialog->mapFromSurface(
                QPointF(geometry.right()-42,geometry.top()+22))).toPoint();
            QTest::mouseMove(&scene.view,save);
            QTest::mouseClick(&scene.view,Qt::LeftButton,{},save);
            QVERIFY2(reached("Picker saved"),qPrintable("File System Access save failed: "+title()));
            QTRY_COMPARE(chooser(),qulonglong(0));
            // The chooser's starting folder is Chromium's choice; accept either.
            QVERIFY2(bytes(home+"/pdtyped.bin")==payload || bytes(downloads+"/pdtyped.bin")==payload,
                     "The save picker did not write the typed file name.");
        }
        QVERIFY(capture(scene.view,chromium ? "native-chromium-script-saves.png" : "native-firefox-script-saves.png"));
        // Read-only guard: nothing may escape the disposable home.
        for (const auto *name : {"pdfixtureblob.bin","pdfixturereport.csv","pdfixturepicker.bin","pdtyped.bin"})
            for (const auto &folder : {QDir::homePath(),QDir::homePath()+"/Downloads"})
                QVERIFY2(!QFileInfo::exists(folder+"/"+name),"A fixture escaped the disposable home.");
        manager.closeWindow(id);
        QTRY_COMPARE_WITH_TIMEOUT(browser.state(),QProcess::NotRunning,10000);
    }
    // Xdg popups that Qt auto-created below a client item (menus, tooltips).
    QList<QWaylandQuickShellSurfaceItem *> popupItems(QQuickItem *client)
    {
        QList<QWaylandQuickShellSurfaceItem *> found;
        const auto visit=[&](auto &&self, QQuickItem *parent) -> void {
            for (auto *child : parent->childItems()) {
                if (auto *item=qobject_cast<QWaylandQuickShellSurfaceItem *>(child))
                    if (auto *xdg=qobject_cast<QWaylandXdgSurface *>(item->shellSurface()); xdg && xdg->popup())
                        found.append(item);
                self(self,child);
            }
        };
        visit(visit,client);
        return found;
    }
    // The reported daily-use path: right-click an image, choose Save Image As
    // from the browser's own xdg_popup menu with the pointer, accept a name.
    void contextMenuSaves(bool chromium)
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        const auto profile=scene.files.path()+"/profile";
        const auto home=scene.files.path()+"/home";
        const auto downloads=home+"/Downloads";
        QVERIFY(QDir().mkpath(downloads));
        if (!chromium) {
            QVERIFY(QDir().mkpath(profile+"/browsers/firefox"));
            QFile prefs(profile+"/browsers/firefox/prefs.js");
            QVERIFY(prefs.open(QIODevice::WriteOnly));
            prefs.write("user_pref(\"browser.aboutwelcome.enabled\",false);\n"
                        "user_pref(\"browser.shell.checkDefaultBrowser\",false);\n"
                        "user_pref(\"browser.startup.homepage_override.mstone\",\"ignore\");\n"
                        "user_pref(\"extensions.enabledScopes\",1);\n"
                        "user_pref(\"extensions.autoDisableScopes\",15);\n"
                        "user_pref(\"datareporting.policy.firstRunURL\",\"\");\n");
            prefs.close();
        }
        QImage image(64,64,QImage::Format_RGB32);image.fill(QColor("#27a88a"));
        QByteArray picture;
        {
            QBuffer buffer(&picture);
            QVERIFY(buffer.open(QIODevice::WriteOnly) && image.save(&buffer,"PNG"));
        }
        const QByteArray page=
            "<!doctype html><title>Loading Menu Fixture</title>"
            "<img src='/photo.png' style='position:fixed;left:0;top:0;width:100%;height:100%'>"
            "<script>addEventListener('contextmenu',()=>{document.title='Context menu requested'});"
            "let loaded=false;const sized=()=>requestAnimationFrame(()=>{"
            "if(loaded)document.title='Menu fixture ready '+innerWidth});"
            "addEventListener('resize',sized);addEventListener('load',()=>{loaded=true;sized()})</script>";
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket=server.nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    auto request=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",request);
                    if (!request.contains("\r\n\r\n")) return;
                    QByteArray status="200 OK", type="text/html", body=page;
                    if (request.startsWith("GET /photo.png ")) {type="image/png";body=picture;}
                    else if (!request.startsWith("GET / ")) {status="404 Not Found";type="text/plain";body={};}
                    socket->write("HTTP/1.1 "+status+"\r\nContent-Type: "+type+"\r\nContent-Length: "+
                                  QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
            }
        });
        frankenstein::applications::LaunchPlan plan;
        plan.program=chromium ? "/usr/bin/chromium" : "/usr/lib/firefox/firefox";
        const auto url=QString("http://127.0.0.1:%1/").arg(server.serverPort());
        plan.arguments=chromium ? QStringList{"--no-first-run","--no-default-browser-check","--new-window",url}
                                : QStringList{"--new-window",url};
        QString error;
        QVERIFY2(prepareApplicationAppearance(profile,error),qPrintable(error));
        QVERIFY2(prepareStyledApplication(plan,profile,error),qPrintable(error));
        auto env=nativeApplicationEnvironment(QProcessEnvironment::systemEnvironment(),
                   QString::fromUtf8(scene.compositor.socketName()),profile,
                   externalApplicationPlugins());
        if (!X11Bridge::helperPath().isEmpty()) {
            QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
            scene.x11.applyTo(env);
        }
        if (!chromium) env.insert("MOZ_GTK_TITLEBAR_DECORATION","client"); // Match the production launch path.
        if (qEnvironmentVariableIsSet("FRANKENSTEIN_FIXTURE_PTRACER")) {
            env.insert("LD_PRELOAD", qEnvironmentVariable("FRANKENSTEIN_FIXTURE_PTRACER"));
            // Firefox otherwise names its crash helper as its only ptracer.
            env.insert("MOZ_CRASHREPORTER_DISABLE", "1");
        }
        if (!chromium && qEnvironmentVariableIsSet("FRANKENSTEIN_FIREFOX_WIDGET_LOG"))
            env.insert("MOZ_LOG","Widget:5,WidgetPopup:5,Focus:5"); // Disposable fixture diagnostics only.
        env.insert("HOME",home);
        silenceFixtureAudio(env,scene.files.path());
        for (const auto &name : {"config","cache","data"}) QVERIFY(QDir().mkpath(profile+"/"+name));
        QProcess browser;
        browser.setProcessEnvironment(env);
        browser.setWorkingDirectory(home);
        const auto prefix=QString(chromium ? "chromium" : "firefox");
        browser.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/"+prefix+"-context-save-stderr.txt");
        browser.setStandardOutputFile(QProcess::nullDevice());
        const auto closeBrowser=qScopeGuard([&] {
            if (browser.state()!=QProcess::NotRunning) {browser.kill();browser.waitForFinished(5000);}
        });
        WireTrace wire(scene.compositor);
        const auto failureCapture=qScopeGuard([&] {
            if(QTest::currentTestFailed()) {
                capture(scene.view,"native-"+prefix+"-context-save-failure.png");
                if (browser.state()==QProcess::Running) printThreadWaits(browser.processId());
            }
            if(QTest::currentTestFailed() || qEnvironmentVariableIsSet("FRANKENSTEIN_WIRE_TRACE"))
                wire.print();
        });
        QElapsedTimer clock;
        clock.start();
        browser.start(plan.program,plan.arguments);
        QVERIFY(browser.waitForStarted());
        qulonglong id=0;
        QVERIFY2(QTest::qWaitFor([&]{
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("title").toString().startsWith("Menu fixture ready"))
                    {id=value.toMap().value("id").toULongLong();return true;}
            return false;
        },20000),"The browser did not load the context-menu fixture.");
        const auto title=[&] {
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("id").toULongLong()==id) return value.toMap().value("title").toString();
            return QString();
        };
        auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client);
        QVERIFY2(QFileInfo(QString("/proc/%1/exe").arg(liveSurfaceClientPid(client->surface()))).canonicalFilePath()
                     .endsWith(chromium ? "/chromium" : "/firefox"),
                 "The context-menu browser unexpectedly used the X11 bridge rather than native Wayland");
        manager.moveWindow(id,130,80);
        manager.resizeWindow(id,800,500);
        const auto sizeReady=[&] {
            for(const auto &entry:manager.snapshot().value("windows").toList())
                if(entry.toMap().value("id").toULongLong()==id)
                    return entry.toMap().value("width").toInt()==800 && entry.toMap().value("height").toInt()==500;
            return false;
        };
        QTRY_VERIFY(sizeReady());
        if (chromium) {
            // Chromium draws its own frame inside a shadow. Dragged to the top, its visible edge
            // (not the shadow) meets the menu bar.
            manager.moveWindow(id,130,0);
            auto *frameItem=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
            auto *frameXdg=frameItem?qobject_cast<QWaylandXdgSurface *>(frameItem->shellSurface()):nullptr;
            QVERIFY(frameXdg && !frameXdg->windowGeometry().topLeft().isNull());
            QTRY_COMPARE(frameItem->mapToScene(frameItem->mapFromSurface(QPointF(frameXdg->windowGeometry().topLeft()))),
                         QPointF(130,26));
            manager.moveWindow(id,130,80);
        }
        // The content process has laid out and painted at the new size.
        QVERIFY2(QTest::qWaitFor([&]{return title().startsWith("Menu fixture ready 800");},5000),
                 qPrintable("The page never settled after the resize; state: "+title()));
        wire.mark("activate");
        manager.activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        // Diagnostics only (never set by a gate): pause between the resize and the right-click,
        // to tell Firefox's own popup-creation hang (a timing race?) from a compositor cause.
        if (const auto settle = qEnvironmentVariableIntValue("FRANKENSTEIN_CONTEXT_MENU_SETTLE_MS"); settle > 0)
            QTest::qWait(qMin(settle, 5000));
        // Twice: the second Save Image As froze Firefox with its chooser behind (psn_tch, 2026-10-22).
        for (int round = 0; round < 2; ++round) {
        const QString saveName = round == 0 ? "pdmenusaved" : "pdmenusecond";
        if (round > 0) { manager.activate(id); QTRY_VERIFY(client->hasActiveFocus()); QTest::qWait(300); }
            const auto point=client->mapToScene(client->mapFromSurface(QPointF(240,260))).toPoint();
            wire.mark("right-click");
            QTest::mouseMove(&scene.view,point);
            QTest::qWait(50);
            QTest::mousePress(&scene.view,Qt::RightButton,{},point);
            QTest::qWait(80);
            QTest::mouseRelease(&scene.view,Qt::RightButton,{},point);
            // Diagnostics for a failure: did the client's input region accept that point?
            // (A stalled Firefox may have left its region excluding it; see HANDOFF 2026-10-21.)
            const bool regionContains=client->inputRegionContains(client->mapFromScene(QPointF(point)));
            QVERIFY2(QTest::qWaitFor([&]{return title().startsWith("Context menu requested");},5000),
                     qPrintable("The page did not receive the right-click; state: "+title()+
                                "; input region contained the point: "+(regionContains?"yes":"no")));
            QVERIFY2(QTest::qWaitFor([&]{return popupItems(client).size()==1 &&
                                                popupItems(client).first()->boundingRect().height()>100;},5000),
                     "The browser's context menu popup never appeared.");
            QTest::qWait(300); // Menus ignore activation for a moment after opening.
            auto *menu=popupItems(client).first();
            const auto bounds=menu->mapRectToScene(menu->boundingRect());
            qInfo()<<"Context popup scene bounds:"<<bounds<<"at"<<clock.elapsed()<<"ms";
            QVERIFY(capture(scene.view,"native-"+prefix+"-context-menu.png"));
            // Both browsers list Open Image in New Tab, then Save Image As. Row
            // metrics (shadow inset plus padding, row height) are each browser's own;
            // inspect the menu capture first if a browser update reorders the menu.
            const auto row=chromium ? QPointF(90,23.6+29.4*1.5) : QPointF(60,8.7+23*1.5);
            const auto item=(bounds.topLeft()+row).toPoint();
            QTest::mouseMove(&scene.view,item-QPoint(0,6));
            QTest::qWait(60);
            QTest::mouseMove(&scene.view,item);
            QTest::qWait(120);
            QVERIFY(capture(scene.view,"native-"+prefix+"-context-menu-hover.png"));
            QTest::mousePress(&scene.view,Qt::LeftButton,{},item);
            // Clicking a popup must not move keyboard focus off the window: Qt then
            // deactivates the toplevel and both browsers dismiss the open menu.
            QCOMPARE(scene.compositor.defaultSeat()->keyboardFocus(),client->surface());
            QTest::qWait(60);
            QTest::mouseRelease(&scene.view,Qt::LeftButton,{},item);
            qInfo()<<"Menu item released at"<<clock.elapsed()<<"ms";
            const auto chooser=[&] {
                for(const auto &value:manager.snapshot().value("windows").toList())
                    if(value.toMap().value("id").toULongLong()!=id) return value.toMap().value("id").toULongLong();
                return qulonglong(0);
            };
            QVERIFY2(QTest::qWaitFor([&]{return chooser()!=0;},8000),
                     qPrintable("Save Image As opened no chooser; popups left: "+
                                QString::number(popupItems(client).size())+"; page state: "+title()));
            QVERIFY2(popupItems(client).isEmpty(),"The context menu stayed open over the chooser.");
            auto *dialog=qobject_cast<QWaylandQuickShellSurfaceItem *>(
                visualItem(scene.view.contentItem(),"client-"+QString::number(chooser())));
            QVERIFY2(dialog && QTest::qWaitFor([&]{return dialog->hasActiveFocus();},5000),
                     "The save chooser never received keyboard focus.");
            auto *dialogSurface=qobject_cast<QWaylandXdgSurface *>(dialog->shellSurface());
            QVERIFY(dialogSurface && dialogSurface->toplevel());
            QVERIFY2(QTest::qWaitFor([&]{
                         const auto size=dialogSurface->windowGeometry().size();
                         return size.width()<=scene.view.width()-12 && size.height()<=scene.view.height()-60
                                && dialogSurface->toplevel()->activated();},5000),
                     "The save chooser was not fitted to the desktop and activated.");
            QTest::qWait(250); // GTK selects the suggested base name after mapping.
            qInfo()<<"Chooser focused at"<<clock.elapsed()<<"ms";
            QVERIFY(capture(scene.view,"native-"+prefix+"-context-chooser.png"));
            QVERIFY(typeAscii(&scene.view,saveName.toLatin1()));
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Return,{},28);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Return,{},28);
            const auto saved=[&] {
                for (const auto &folder : {home,downloads})
                    for (const auto &name : {saveName+".png",saveName})
                        if (bytes(folder+"/"+name)==picture) return true;
                return false;
            };
            if (chromium && !QTest::qWaitFor(saved,2500)) {
                // Record whether Return reached Chromium's GTK chooser, then press its
                // Save button as the existing picker workflow does.
                qInfo()<<"Chromium chooser did not save on Return";
                QVERIFY(capture(scene.view,"native-chromium-context-return.png"));
                const auto geometry=dialogSurface->windowGeometry();
                const auto save=dialog->mapToScene(dialog->mapFromSurface(
                    QPointF(geometry.right()-42,geometry.top()+22))).toPoint();
                QTest::mouseMove(&scene.view,save);
                QTest::mouseClick(&scene.view,Qt::LeftButton,{},save);
            }
            QVERIFY2(QTest::qWaitFor(saved,8000),"Save Image As did not write the image under the typed name.");
            qInfo()<<"Image saved at"<<clock.elapsed()<<"ms";
            QTRY_COMPARE(chooser(),qulonglong(0));
            qInfo()<<"Chooser closed at"<<clock.elapsed()<<"ms";
            QTRY_VERIFY(client->hasActiveFocus());
            qInfo()<<"Browser refocused at"<<clock.elapsed()<<"ms";
        }
        for (const auto *name : {"pdmenusaved.png","pdmenusaved","pdmenusecond.png","pdmenusecond"})
            for (const auto &folder : {QDir::homePath(),QDir::homePath()+"/Downloads"})
                QVERIFY2(!QFileInfo::exists(folder+"/"+name),"A fixture escaped the disposable home.");
        QTest::qWait(300);
        qInfo()<<"Popups open before close:"<<popupItems(client).size();
        QVERIFY(capture(scene.view,"native-"+prefix+"-context-saved.png"));
        manager.closeWindow(id);
        const bool closed=QTest::qWaitFor([&]{return browser.state()==QProcess::NotRunning;},6000);
        if (!closed) {
            qInfo()<<"Browser still open 6 s after close; popups:"<<popupItems(client).size()
                   <<"windows:"<<manager.rowCount();
            QVERIFY(capture(scene.view,"native-"+prefix+"-context-close-ignored.png"));
        }
        QVERIFY2(closed,"The browser ignored the window close request after saving.");
    }
  private slots:
    void destroyedCommittedBufferCanRepaintButNullAttachmentUnmaps()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        LineProcess probe;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath()+"/pointer-probe",{});
        QVERIFY(probe.process.waitForStarted());
        QVERIFY(!probe.next("ready").isEmpty());
        QTRY_VERIFY(!first(*scene.manager,"Pointer Probe").isEmpty());
        const auto id=first(*scene.manager,"Pointer Probe").value("id").toULongLong();
        QVERIFY(probe.send("replace-buffer","buffer-recovered"));
        QTRY_COMPARE(first(*scene.manager,"Pointer Probe").value("width").toInt(),320);
        QTRY_COMPARE(first(*scene.manager,"Pointer Probe").value("height").toInt(),240);
        QCOMPARE(first(*scene.manager,"Pointer Probe").value("id").toULongLong(),id);
        QVERIFY(capture(scene.view,"recovered-committed-buffer.png"));
        QVERIFY(probe.send("unmap","unmapped"));
        QTRY_VERIFY(first(*scene.manager,"Pointer Probe").isEmpty());
        probe.process.write("quit\n");
        QTRY_COMPARE(probe.process.state(),QProcess::NotRunning);
        QCOMPARE(probe.process.exitCode(),0);
    }
#ifdef PLATINUM_MULTI_DISPLAY_EXPERIMENT
    void nativeClientsSeeMonitorsAndKeepOutputMembership()
    {
        const auto ipc=qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."),"Requires owned two-output venue");
        NativeScene scene;
        OutputTrace wire(scene.compositor);
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg",{"-s",ipc,QString("[pid=%1] fullscreen enable global").arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(),0);
        QTRY_COMPARE(scene.view.size(),QSize(2080,760));
        const QVector<DisplayOutput> outputs{{"HEADLESS-1",{0,0,1120,760},1.5,"normal"},
                                             {"HEADLESS-2",{1120,0,960,600},2,"90"}};
        const auto layout=DisplayLayout::create(outputs,"HEADLESS-1",DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        scene.manager->setDisplayLayout(*layout);
        QTRY_COMPARE(([&]{const auto values=scene.compositor.outputs();return std::count_if(values.cbegin(),values.cend(),[](auto *output){return !output->property("platinumRetired").toBool();});})(),2);
        QProcess probe;
        auto probeEnvironment=scene.childEnvironment();
        if(qEnvironmentVariableIsSet("FRANKENSTEIN_OUTPUT_TRACE")) {
            probeEnvironment.insert("WAYLAND_DEBUG","client");
            probe.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/owned-output-client-trace.txt");
        }
        probe.setProcessEnvironment(probeEnvironment);
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"Outputs"});
        QVERIFY(probe.waitForStarted());
        auto &manager=*scene.manager;
        const auto diagnose=qScopeGuard([&]{if(QTest::currentTestFailed()) {
            qWarning()<<"Output probe process"<<probe.state()<<probe.exitCode()<<probe.readAllStandardError().right(18000);
            qWarning()<<"Output failure state"<<QJsonDocument::fromVariant(manager.snapshot()).toJson(QJsonDocument::Compact);
            for(auto *surface:scene.compositor.surfaces()) for(auto *view:surface->views()) {
                auto *item=qobject_cast<QWaylandQuickItem *>(view->renderObject());
                if(item) qWarning()<<"Output surface"<<liveSurfaceClientPid(surface)<<surface->hasContent()
                    <<item->isVisible()<<item->mapRectToScene(item->boundingRect())
                    <<(view->output()?view->output()->property("platinumDisplayName"):QVariant())
                    <<(view->output()?bool(view->output()->resourceForClient(surface->client())):false);
            }
            capture(scene.view,"output-membership-failure.png");
        }});
        QTRY_VERIFY(!first(manager,"Native Probe Outputs").isEmpty());
        QDBusInterface api("org.frankenstein.NativeProbeOutputs","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        const auto screens=[&]{return QJsonDocument::fromJson(QDBusReply<QString>(api.call("Screens")).value().toUtf8()).array();};
        const auto screen=[&](QString name) {
            for(const auto &value:screens()) if(value.toObject().value("name").toString()==name) return value.toObject();
            return QJsonObject();
        };
        QTRY_COMPARE(screens().size(),2);
        QTRY_COMPARE(screen("HEADLESS-1").value("width").toInt(),1120);
        QCOMPARE(screen("HEADLESS-1").value("height").toInt(),760);
        QCOMPARE(screen("HEADLESS-1").value("x").toInt(),0);
        QCOMPARE(screen("HEADLESS-1").value("dpr").toDouble(),1.0);
        QCOMPARE(screen("HEADLESS-2").value("width").toInt(),960);
        QCOMPARE(screen("HEADLESS-2").value("height").toInt(),600);
        QCOMPARE(screen("HEADLESS-2").value("x").toInt(),1120);
        QCOMPARE(screen("HEADLESS-2").value("dpr").toDouble(),1.0);
        const auto currentId=[&]{return first(manager,"Native Probe Outputs").value("id").toULongLong();};
        const auto pid=pid_t(probe.processId());
        manager.moveWindow(currentId(),100,80);
        QTRY_COMPARE(wire.outputs(pid),QSet<QString>{"HEADLESS-1"});
        QTRY_COMPARE(QDBusReply<QString>(api.call("ScreenName")).value(),QString("HEADLESS-1"));
        manager.moveWindow(currentId(),900,80);
        QTRY_COMPARE(wire.outputs(pid),(QSet<QString>{"HEADLESS-1","HEADLESS-2"}));
        manager.moveWindow(currentId(),1030,80); // Dominant output changes, both remain entered.
        QTRY_COMPARE(first(manager,"Native Probe Outputs").value("x").toInt(),1030);
        scene.view.update();
        QTest::qWait(60);
        QCOMPARE(wire.outputs(pid),(QSet<QString>{"HEADLESS-1","HEADLESS-2"}));
        QCOMPARE(wire.unbalanced,0);
        manager.moveWindow(currentId(),1250,80);
        QTRY_COMPARE(wire.outputs(pid),QSet<QString>{"HEADLESS-2"});
        QTRY_COMPARE(QDBusReply<QString>(api.call("ScreenName")).value(),QString("HEADLESS-2"));
        // Ask for the *other* output explicitly. Geometry-based guessing cannot pass.
        QVERIFY(QDBusReply<bool>(api.call("FullScreenOn","HEADLESS-1")).value());
        QTRY_COMPARE(wire.lastFullscreen,QString("HEADLESS-1"));
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),1120);
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),760);
        QTRY_VERIFY(first(manager,"Native Probe Outputs").value("fullScreen").toBool());
        QTRY_COMPARE(wire.outputs(pid),QSet<QString>{"HEADLESS-1"});
        api.call("SetFullScreen",false);
        QTRY_VERIFY(!first(manager,"Native Probe Outputs").value("fullScreen").toBool());
        QVERIFY(QDBusReply<bool>(api.call("FullScreenOn","HEADLESS-2")).value());
        QTRY_COMPARE(wire.lastFullscreen,QString("HEADLESS-2"));
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),960);
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),600);
        QTRY_COMPARE(wire.outputs(pid),QSet<QString>{"HEADLESS-2"});
        QVERIFY(capture(scene.view,"real-output-selected-fullscreen.png"));
        QTRY_COMPARE(QDBusReply<QString>(api.call("ScreenName")).value(),QString("HEADLESS-2"));
        // Removing the original first output must remove its global, rather
        // than disguising it as the remaining connector or retaining a canvas.
        const auto secondary=DisplayLayout::create({outputs[1]},"HEADLESS-2",DisplayLayout::Mode::Extend);
        const auto primary=DisplayLayout::create({outputs[0]},"HEADLESS-1",DisplayLayout::Mode::Extend);
        QVERIFY(secondary && primary);
        LineProcess late;
        late.process.setProcessEnvironment(scene.childEnvironment());
        late.process.start(QCoreApplication::applicationDirPath()+"/output-race-probe",{});
        QVERIFY(late.process.waitForStarted());
        QCOMPARE(late.next("announced"),QString("announced 2"));
        scene.manager->setDisplayLayout(*secondary);
        QTRY_COMPARE(screens().size(),1);
        QVERIFY(late.send("bind","bound 2 done 2"));
        QTRY_COMPARE(late.process.state(),QProcess::NotRunning);
        QCOMPARE(late.process.exitCode(),0);
        QTRY_COMPARE(screen("HEADLESS-2").value("x").toInt(),0);
        QCOMPARE(screen("HEADLESS-2").value("width").toInt(),960);
        QTRY_COMPARE(wire.outputs(pid),QSet<QString>{"HEADLESS-2"});
        scene.manager->setDisplayLayout(*primary); // Removes the requested fullscreen output too.
        QTRY_COMPARE(screen("HEADLESS-1").value("width").toInt(),1120);
        QTRY_COMPARE(screens().size(),1);
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),1120);
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),760);
        QTRY_COMPARE(wire.outputs(pid),QSet<QString>{"HEADLESS-1"});
        for(int cycle=0;cycle<3;++cycle) {
            scene.manager->setDisplayLayout(*layout);
            QTRY_COMPARE(screens().size(),2);
            scene.manager->setDisplayLayout(*primary);
            QTRY_COMPARE(screens().size(),1);
        }
        QCOMPARE(wire.unbalanced,0);
        if(!X11Bridge::helperPath().isEmpty()) QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        api.call("SetFullScreen",false);
        QTRY_VERIFY(!first(manager,"Native Probe Outputs").value("fullScreen").toBool());
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
        QTRY_COMPARE(manager.rowCount(),0);
        qInfo()<<"Client output observations and balanced wl_surface membership passed";
    }
    void initialFullscreenCanSelectTheSecondOutput()
    {
        NativeScene scene;
        OutputTrace wire(scene.compositor);
        QVERIFY(scene.show());
        const auto ipc=qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY(ipc.startsWith("/tmp/platinum-headless."));
        QProcess command;
        command.start("swaymsg",{"-s",ipc,QString("[pid=%1] fullscreen enable global").arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(),0);
        QTRY_COMPARE(scene.view.size(),QSize(2080,760));
        const auto layout=DisplayLayout::create({{"HEADLESS-1",{0,0,1120,760},1.5,"normal"},
                                                 {"HEADLESS-2",{1120,0,960,600},2,"90"}},
                                                "HEADLESS-1",DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        scene.manager->setDisplayLayout(*layout);
        QTRY_COMPARE(([&]{const auto values=scene.compositor.outputs();return std::count_if(values.cbegin(),values.cend(),[](auto *output){return !output->property("platinumRetired").toBool();});})(),2);
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"OutputInitial","--screen=HEADLESS-2","--start-fullscreen"});
        QVERIFY(probe.waitForStarted());
        QDBusInterface api("org.frankenstein.NativeProbeOutputInitial","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        QTRY_COMPARE(wire.lastFullscreen,QString("HEADLESS-2"));
        QTRY_VERIFY(first(*scene.manager,"Native Probe OutputInitial").value("fullScreen").toBool());
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),960);
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),600);
        const auto framePosition=[&]{
            const auto id=first(*scene.manager,"Native Probe OutputInitial").value("id").toULongLong();
            auto *frame=visualItem(scene.view.contentItem(),"frame-"+QString::number(id));
            return frame?frame->position():QPointF(-1,-1);
        };
        QTRY_COMPARE(framePosition(),QPointF(1120,0));
        QTRY_COMPARE(wire.outputs(pid_t(probe.processId())),QSet<QString>{"HEADLESS-2"});
        QCOMPARE(wire.unbalanced,0);
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
    }
    void x11ApplicationsSeeBothMonitorsAndUseTheSecond()
    {
        // X11 programs (games, older tools) choose monitors through RandR, which the private X11
        // bridge derives from this compositor's per-monitor outputs.
        if (X11Bridge::helperPath().isEmpty()) QSKIP("This build explicitly disables X11 support.");
        const auto ipc=qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."),"Requires the owned two-output venue");
        NativeScene scene;
        OutputTrace wire(scene.compositor);
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg",{"-s",ipc,QString("[pid=%1] fullscreen enable global").arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(),0);
        QTRY_COMPARE(scene.view.size(),QSize(2080,760));
        const auto layout=DisplayLayout::create({{"HEADLESS-1",{0,0,1120,760},1.5,"normal"},
                                                 {"HEADLESS-2",{1120,0,960,600},2,"90"}},
                                                "HEADLESS-1",DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        scene.manager->setDisplayLayout(*layout);
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),12000);
        auto &manager=*scene.manager;
        const auto failureCapture=qScopeGuard([&]{
            if(QTest::currentTestFailed()) {
                qWarning()<<"X11 two-screen state"<<QJsonDocument::fromVariant(manager.snapshot()).toJson(QJsonDocument::Compact);
                capture(scene.view,"x11-two-screen-failure.png");
            }
        });
        // RandR as any X11 program sees it.
        const auto monitors=[&]{
            QProcess xrandr;
            xrandr.setProcessEnvironment(scene.childEnvironment());
            xrandr.start("/usr/bin/xrandr",{"--listmonitors"});
            return xrandr.waitForFinished(3000)?QString::fromUtf8(xrandr.readAllStandardOutput()):QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(monitors().startsWith("Monitors: 2"),8000);
        // "+0+0" and "+1120+0": the two monitors side by side, as the desktop arranges them. Just
        // after start-up the bridge briefly lists them as 0x0 at the origin. The physical size
        // is "-1" millimetres (unknown); X11 programs then assume the default DPI.
        const auto arranged=[](const QString &listed){
            return listed.contains(QRegularExpression(R"(\bHEADLESS-1 1120/-?\d+x760/-?\d+\+0\+0\b)")) &&
                   listed.contains(QRegularExpression(R"(\bHEADLESS-2 960/-?\d+x600/-?\d+\+1120\+0\b)"));
        };
        const bool settled=QTest::qWaitFor([&]{return arranged(monitors());},8000);
        qInfo().noquote()<<"X11 monitors:"<<monitors();
        QVERIFY2(settled,qPrintable(monitors()));

        QProcess probe;
        auto environment=scene.childEnvironment();
        environment.insert("QT_QPA_PLATFORM","xcb");
        probe.setProcessEnvironment(environment);
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"X11Screens"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager,"Native Probe X11Screens").isEmpty(),10000);
        QDBusInterface api("org.frankenstein.NativeProbeX11Screens","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        const auto screens=[&]{return QJsonDocument::fromJson(QDBusReply<QString>(api.call("Screens")).value().toUtf8()).array();};
        QTRY_COMPARE(screens().size(),2);
        qInfo().noquote()<<"X11 client screens:"<<QJsonDocument(screens()).toJson(QJsonDocument::Compact);
        QString second;
        for(const auto &value:screens())
            if(value.toObject().value("x").toInt()==1120) second=value.toObject().value("name").toString();
        QVERIFY2(!second.isEmpty(),"No X11 screen at the second monitor's position");
        // An X11 program picks a monitor by moving its window onto it, then going full screen.
        // The bridge does not apply the move (the desktop places windows) but must honour it.
        api.call("MoveTo",1200,100);
        QTest::qWait(200);
        api.call("SetFullScreen",true);
        QTRY_VERIFY(first(manager,"Native Probe X11Screens").value("fullScreen").toBool());
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),960);
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),600);
        const auto id=first(manager,"Native Probe X11Screens").value("id").toULongLong();
        auto *frame=visualItem(scene.view.contentItem(),"frame-"+QString::number(id));
        QVERIFY(frame);
        QTRY_COMPARE(frame->position(),QPointF(1120,0));
        LineProcess pointer;
        pointer.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver",{});
        QVERIFY(!pointer.next("driver-ready").isEmpty());
        QVERIFY(pointer.send("abs 1500 300 2080 760","ok"));
        QVERIFY(pointer.send("button 272 1","ok"));
        QVERIFY(pointer.send("button 272 0","ok"));
        QTRY_VERIFY(QDBusReply<int>(api.call("MousePresses")).value()>0);
        QVERIFY(capture(scene.view,"x11-second-monitor-fullscreen.png"));
        api.call("SetFullScreen",false);
        QTRY_VERIFY(!first(manager,"Native Probe X11Screens").value("fullScreen").toBool());
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
    }
    void fullScreenWithoutAMonitorStaysOnTheWindowsMonitor()
    {
        // GTK, browsers' F11 and video players ask for full screen without naming a monitor:
        // the window must fill the monitor it is on, not jump to the main one.
        const auto ipc=qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."),"Requires the owned two-output venue");
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg",{"-s",ipc,QString("[pid=%1] fullscreen enable global").arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(),0);
        QTRY_COMPARE(scene.view.size(),QSize(2080,760));
        const auto layout=DisplayLayout::create({{"HEADLESS-1",{0,0,1120,760},1.5,"normal"},
                                                 {"HEADLESS-2",{1120,0,960,600},2,"90"}},
                                                "HEADLESS-1",DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        scene.manager->setDisplayLayout(*layout);
        auto &manager=*scene.manager;
        LineProcess probe;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath()+"/pointer-probe",{"--resizable"});
        QVERIFY(!probe.next("ready").isEmpty());
        QVariantMap window;
        QTRY_VERIFY(!(window=first(manager,"Pointer Probe")).isEmpty());
        const auto id=window.value("id").toULongLong();
        const auto frame=[&]{auto *item=visualItem(scene.view.contentItem(),"frame-"+QString::number(id));
                             return item?item->position():QPointF(-1,-1);};
        const auto failure=qScopeGuard([&]{if(QTest::currentTestFailed()){
            qWarning()<<"Probe"<<probe.lines.join(" | ")<<"window"<<first(manager,"Pointer Probe");
            capture(scene.view,"fullscreen-null-output-failure.png");}});
        for(const auto &[x,screen,size] : {std::tuple{1250,QPointF(1120,0),QSize(960,600)},
                                           std::tuple{100,QPointF(0,0),QSize(1120,760)}}) {
            manager.moveWindow(id,x,80);
            QTRY_COMPARE(first(manager,"Pointer Probe").value("x").toInt(),x);
            probe.skip();
            QVERIFY(probe.send("fullscreen on","fullscreen-set"));
            QTRY_VERIFY(first(manager,"Pointer Probe").value("fullScreen").toBool());
            QTRY_COMPARE(frame(),screen);
            QTest::qWait(100);
            // Every full-screen configure has this monitor's size: no interim jump to the other.
            const auto configures=probe.pending("configure");
            QVERIFY(!configures.isEmpty());
            for(const auto &line:configures)
                QVERIFY2(line==QString("configure %1 %2 fullscreen").arg(size.width()).arg(size.height()),
                         qPrintable(configures.join(" | ")));
            QVERIFY(capture(scene.view,QString("fullscreen-null-output-%1.png").arg(x)));
            probe.skip();
            QVERIFY(probe.send("fullscreen off","fullscreen-set"));
            QTRY_VERIFY(!first(manager,"Pointer Probe").value("fullScreen").toBool());
            // Qt restores the windowed position when the app acknowledges the normal size; let
            // that land before the next move (a person cannot move it that quickly).
            QVERIFY(!probe.next("configure 320 240").isEmpty());
            QTest::qWait(150);
            QTRY_COMPARE(frame().x(),qreal(x));
        }
        probe.process.write("quit\n");
        QVERIFY(probe.process.waitForFinished(3000));
    }
    void popupMenusStayOnTheirMonitor()
    {
        // A menu opened near a monitor's edge must be flipped or slid onto that monitor. Monitor
        // two is shorter (600) than the spanning desktop (760): spilling below 600 would put the
        // menu where no screen shows it.
        const auto ipc=qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."),"Requires the owned two-output venue");
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg",{"-s",ipc,QString("[pid=%1] fullscreen enable global").arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(),0);
        QTRY_COMPARE(scene.view.size(),QSize(2080,760));
        const QRect first_screen(0,0,1120,760), second_screen(1120,0,960,600);
        const auto layout=DisplayLayout::create({{"HEADLESS-1",first_screen,1.5,"normal"},
                                                 {"HEADLESS-2",second_screen,2,"90"}},
                                                "HEADLESS-1",DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        scene.manager->setDisplayLayout(*layout);
        auto &manager=*scene.manager;
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"Popups"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY(!first(manager,"Native Probe Popups").isEmpty());
        QDBusInterface api("org.frankenstein.NativeProbePopups","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        const auto id=first(manager,"Native Probe Popups").value("id").toULongLong();
        auto *client=visualItem(scene.view.contentItem(),"client-"+QString::number(id));
        QVERIFY(client);
        const auto menuRect=[&]{
            const auto popups=popupItems(client);
            return popups.size()==1 && popups.first()->surface() && popups.first()->surface()->hasContent()
                       ? popups.first()->mapRectToScene(popups.first()->boundingRect()) : QRectF();
        };
        const auto failure=qScopeGuard([&]{if(QTest::currentTestFailed()){
            qWarning()<<"Popup rect"<<menuRect()<<"window"<<first(manager,"Native Probe Popups");
            capture(scene.view,"popup-monitor-failure.png");}});
        // Popups need a recent input serial (a real click), as when a user opens a menu.
        LineProcess pointer;
        pointer.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver",{});
        QVERIFY(!pointer.next("driver-ready").isEmpty());
        const auto clickWindow=[&]{
            const auto window=first(manager,"Native Probe Popups");
            const auto x=window.value("x").toInt()+180, y=window.value("y").toInt()+140;
            return pointer.send(QString("abs %1 %2 2080 760").arg(x).arg(y),"ok") &&
                   pointer.send("button 272 1","ok") && pointer.send("button 272 0","ok");
        };
        // Bottom-right corner of monitor two: the menu would cross both its right and bottom edges.
        manager.moveWindow(id,2080-370,600-250);
        manager.activate(id);
        QTRY_VERIFY(first(manager,"Native Probe Popups").value("active").toBool());
        QVERIFY(clickWindow());
        QTRY_VERIFY(QDBusReply<int>(api.call("MousePresses")).value()>0);
        api.call("ShowFileMenuAt",350,220);
        QTRY_VERIFY(!menuRect().isEmpty());
        QTest::qWait(150);
        qInfo()<<"Menu near monitor two's corner:"<<menuRect();
        QVERIFY2(QRectF(second_screen).contains(menuRect()),qPrintable(QString("menu %1,%2 %3x%4 is not on monitor two")
                     .arg(menuRect().x()).arg(menuRect().y()).arg(menuRect().width()).arg(menuRect().height())));
        QVERIFY(capture(scene.view,"popup-on-monitor-two.png"));
        api.call("HideFileMenu");
        QTRY_VERIFY(menuRect().isEmpty());
        // Monitor one's right edge, next to monitor two: the menu stays on monitor one.
        manager.moveWindow(id,1120-370,100);
        QTRY_COMPARE(first(manager,"Native Probe Popups").value("x").toInt(),1120-370);
        const auto presses=QDBusReply<int>(api.call("MousePresses")).value();
        QVERIFY(clickWindow());
        QTRY_VERIFY(QDBusReply<int>(api.call("MousePresses")).value()>presses);
        api.call("ShowFileMenuAt",350,30);
        QTRY_VERIFY(!menuRect().isEmpty());
        QTest::qWait(150);
        qInfo()<<"Menu at monitor one's right edge:"<<menuRect();
        QVERIFY2(QRectF(first_screen).contains(menuRect()),qPrintable(QString("menu %1,%2 %3x%4 is not on monitor one")
                     .arg(menuRect().x()).arg(menuRect().y()).arg(menuRect().width()).arg(menuRect().height())));
        api.call("HideFileMenu");
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
    }
    void mirrorTouchPreservesHostCoordinatesAndContactMetadata()
    {
        // A numeric contract, independent of the native-client round trip: the
        // fitted copy is half size at (0,150), canonical source begins at (500,0).
        QQuickView window;
        window.resize(1300,600);
        const auto layout = DisplayLayout::create({{"copy",{0,0,400,600},1,"normal"},
                                                   {"source",{500,0,800,600},1,"normal"}},
                                                  "source", DisplayLayout::Mode::Mirror);
        QVERIFY(layout);
        ObservedMirrorTouch observed;
        // This numeric contract intercepts delivery; actual QPA -> Quick ->
        // nested application behavior is covered by the two native rows below.
        observed.consume = true;
        window.installEventFilter(&observed);
        const auto unobserve = qScopeGuard([&] { window.removeEventFilter(&observed); });
        MirrorTouch router(&window);
        QVERIFY(router.setLayout(*layout));
        auto *device = QTest::createTouchDevice();
        const auto point = [&](QEventPoint::State state, QPointF at, ulong time) {
            auto p = QMutableEventPoint::withTimeStamp(time, 7, state, at, at, window.mapToGlobal(at));
            QMutableEventPoint::setDevice(p, device);
            QMutableEventPoint::setPressure(p, 0.42);
            QMutableEventPoint::setRotation(p, 17);
            QMutableEventPoint::setEllipseDiameters(p, {8,4});
            QMutableEventPoint::setVelocity(p, {3,4});
            QMutableEventPoint::setUniqueId(p, QPointingDeviceUniqueId::fromNumericId(42));
            return p;
        };
        const auto original = point(QEventPoint::Pressed, {60,200}, 1234);
        QTouchEvent down(QEvent::TouchBegin, device, Qt::ShiftModifier, {original});
        down.setTimestamp(1234);
        QCoreApplication::sendEvent(&window, &down);
        QCOMPARE(observed.events.size(), size_t(1));
        const auto &mapped = observed.events.back()->points().first();
        QCOMPARE(mapped.id(), 7);
        QCOMPARE(mapped.scenePosition(), QPointF(620,100));
        QCOMPARE(mapped.position(), QPointF(620,100));
        QCOMPARE(mapped.globalPosition(), window.mapToGlobal(QPointF(620,100)));
        QCOMPARE(mapped.pressure(), 0.42);
        QCOMPARE(mapped.rotation(), qreal(17));
        QCOMPARE(mapped.ellipseDiameters(), QSizeF(16,8));
        QCOMPARE(mapped.velocity(), QVector2D(6,8));
        QCOMPARE(mapped.uniqueId().numericId(), qint64(42));
        QCOMPARE(observed.events.back()->modifiers(), Qt::ShiftModifier);
        QCOMPARE(observed.events.back()->timestamp(), ulong(1234));
        QCOMPARE(mapped.timestamp(), ulong(1234));
        // Positions of both the shared source point and the actual host event
        // remain physical; accepting the filtered event may change acceptance.
        QCOMPARE(original.position(), QPointF(60,200));
        QCOMPARE(original.scenePosition(), QPointF(60,200));
        QCOMPARE(down.points().first().scenePosition(), QPointF(60,200));
        QCOMPARE(down.points().first().ellipseDiameters(), QSizeF(8,4));
        QTouchEvent moved(QEvent::TouchUpdate, device, Qt::ShiftModifier,
                          {point(QEventPoint::Updated, {80,220}, 1250)});
        moved.setTimestamp(1250);
        QCoreApplication::sendEvent(&window, &moved);
        QCOMPARE(observed.events.back()->points().first().scenePosition(), QPointF(660,140));
        QCOMPARE(observed.events.back()->points().first().scenePressPosition(), QPointF(620,100));
        QCOMPARE(observed.events.back()->points().first().sceneLastPosition(), QPointF(620,100));
        QTouchEvent cancelled(QEvent::TouchCancel, device);
        cancelled.setTimestamp(1260);
        QCoreApplication::sendEvent(&window, &cancelled);
        QCOMPARE(observed.events.back()->type(), QEvent::TouchCancel);
        QCOMPARE(observed.events.back()->timestamp(), ulong(1260));
        QVERIFY(!router.hasActiveContacts());
        const auto afterCancel = observed.events.size();
        QCoreApplication::sendEvent(&window, &moved); // Late update cannot recreate a press.
        QCOMPARE(observed.events.size(), afterCancel);
        QTouchEvent up(QEvent::TouchEnd, device, {}, {point(QEventPoint::Released, {80,220}, 1270)});
        QCoreApplication::sendEvent(&window, &up);
        QCOMPARE(observed.events.size(), afterCancel);
        QTouchEvent gap(QEvent::TouchBegin, device, {}, {point(QEventPoint::Pressed, {450,100}, 1280)});
        QCoreApplication::sendEvent(&window, &gap);
        QCoreApplication::sendEvent(&window, &moved);
        QCoreApplication::sendEvent(&window, &up);
        QCOMPARE(observed.events.size(), afterCancel);
        QVERIFY(!router.hasActiveContacts());
        // The source's far edge is half-open too: x=1300 cannot target content.
        QTouchEvent edge(QEvent::TouchBegin, device, {}, {point(QEventPoint::Pressed, {1300,100}, 1300)});
        QCoreApplication::sendEvent(&window, &edge);
        QCoreApplication::sendEvent(&window, &up);
        QCOMPARE(observed.events.size(), afterCancel);
    }
    void mirrorTouchesReachApplicationsAndShellFromEitherPicture_data()
    {
        QTest::addColumn<bool>("rightPrimary");
        QTest::newRow("source left") << false;
        QTest::newRow("source right") << true;
    }
    void mirrorTouchesReachApplicationsAndShellFromEitherPicture()
    {
        QFETCH(bool, rightPrimary);
        const auto ipc = qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."), "Requires the owned two-output venue");
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg", {"-s", ipc, QString("[pid=%1] fullscreen enable global")
                                                    .arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(), 0);
        QTRY_COMPARE(scene.view.size(), QSize(2080,760));
        const QVector<DisplayOutput> outputs{{"HEADLESS-1", {0,0,1120,760}, 1.5, "normal"},
                                             {"HEADLESS-2", {1120,0,960,600}, 2, "90"}};
        const auto primary = rightPrimary ? QString("HEADLESS-2") : QString("HEADLESS-1");
        const auto layout = DisplayLayout::create(outputs, primary, DisplayLayout::Mode::Mirror);
        const auto extended = DisplayLayout::create(outputs, primary, DisplayLayout::Mode::Extend);
        const auto replacement = DisplayLayout::create(outputs, rightPrimary ? "HEADLESS-1" : "HEADLESS-2",
                                                        DisplayLayout::Mode::Mirror);
        QVERIFY(layout && extended && replacement);
        auto &manager = *scene.manager;
        manager.setDisplayLayout(*extended); // Keep the canonical source's real host origin.
        const QRectF source = rightPrimary ? outputs[1].logical : outputs[0].logical;
        const auto picture = layout->mirrorPicture(rightPrimary ? "HEADLESS-1" : "HEADLESS-2");
        QQmlComponent component(scene.view.engine());
        component.setData("import QtQuick\nShaderEffectSource { live:true; recursive:false; smooth:true }", QUrl());
        std::unique_ptr<QObject> mirror(component.create());
        QVERIFY2(mirror, qPrintable(component.errorString()));
        auto *copy = qobject_cast<QQuickItem *>(mirror.get());
        QVERIFY(copy);
        copy->setParentItem(scene.view.contentItem());
        copy->setPosition(picture.topLeft()); copy->setSize(picture.size()); copy->setZ(1000000);
        QVERIFY(copy->setProperty("sourceItem", QVariant::fromValue(scene.view.rootObject())));
        QVERIFY(copy->setProperty("sourceRect", source));
        ObservedMirrorTouch observed;
        scene.view.installEventFilter(&observed);
        const auto unobserve = qScopeGuard([&] { scene.view.removeEventFilter(&observed); });
        MirrorTouch touch(&scene.view);
        QVERIFY(!touch.setLayout(*extended));
        QVERIFY(touch.setLayout(*layout));
        auto *device = QTest::createTouchDevice();
        const auto onCopy = [&](QPointF at) {
            return picture.topLeft() + (at - source.topLeft()) * (picture.width() / source.width());
        };
        auto *host = manager.findChild<HostPointer *>();
        QVERIFY(host && host->available());
        host->setDesktopRegion(QRegion(source.toRect()));
        const auto unconfine = qScopeGuard([&] { host->setDesktopRegion({}); });
        LineProcess probe, driver;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        QVERIFY(!probe.next("ready").isEmpty());
        QTRY_VERIFY(!first(manager, "Pointer Probe").isEmpty());
        const auto id = first(manager, "Pointer Probe").value("id").toULongLong();
        manager.moveWindow(id, source.x() + 250, 160);
        manager.activate(id);
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        const auto at = client->mapToScene(client->mapFromSurface({100,80}));
        const auto to = client->mapToScene(client->mapFromSurface({140,110}));
        driver.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY(!driver.next("driver-ready").isEmpty());
        QVERIFY(driver.send(QString("abs %1 %2 2080 760").arg(qRound(at.x())).arg(qRound(at.y())), "ok"));
        QTRY_VERIFY(host->desktopConfinementEngaged());
        const auto near = [](const QString &line, QPointF expected) {
            const auto fields = line.split(' ');
            return fields.size() == 4 && qAbs(fields[2].toDouble() - expected.x()) < 2 &&
                                        qAbs(fields[3].toDouble() - expected.y()) < 2;
        };
        for (bool copied : {false, true}) {
            const auto downAt = copied ? onCopy(at) : at;
            const auto moveTo = copied ? onCopy(to) : to;
            QTest::touchEvent(&scene.view, device).press(0, downAt.toPoint(), &scene.view);
            QVERIFY2(near(probe.next("touch-down"), {100,80}), qPrintable(probe.lines.join(" | ")));
            QVERIFY(touch.hasActiveContacts());
            QVERIFY(!touch.setLayout(*replacement)); // Cannot remap a held contact.
            QTest::touchEvent(&scene.view, device).move(0, moveTo.toPoint(), &scene.view);
            QVERIFY2(near(probe.next("touch-motion"), {140,110}), qPrintable(probe.lines.join(" | ")));
            const auto &point = observed.events.back()->points().first();
            QVERIFY(QLineF(point.scenePosition(), to).length() < 2);
            QVERIFY(QLineF(point.scenePressPosition(), at).length() < 2);
            QVERIFY(QLineF(point.sceneLastPosition(), at).length() < 2);
            QCOMPARE(observed.events.back()->pointingDevice(), device);
            QTest::touchEvent(&scene.view, device).release(0, moveTo.toPoint(), &scene.view);
            QVERIFY(!probe.next("touch-up").isEmpty());
            QVERIFY(!touch.hasActiveContacts());
        }
        // Press in a letterbox, then enter the picture: never become a late press.
        const QPoint margin = rightPrimary ? QPoint(500,5) : QPoint(1125,250);
        const auto count = observed.events.size();
        QTest::touchEvent(&scene.view, device).press(0, margin, &scene.view);
        QTest::touchEvent(&scene.view, device).move(0, onCopy(at).toPoint(), &scene.view);
        QTest::touchEvent(&scene.view, device).release(0, onCopy(at).toPoint(), &scene.view);
        QCOMPARE(observed.events.size(), count);
        QVERIFY(!touch.hasActiveContacts());
        // A second contact may enter while a rejected one remains held. The
        // accepted stream begins/ends independently of the raw device's phases.
        QTest::touchEvent(&scene.view, device).press(0, margin, &scene.view);
        QTest::touchEvent(&scene.view, device).stationary(0).press(1, onCopy(at).toPoint(), &scene.view);
        QVERIFY2(near(probe.next("touch-down"), {100,80}), qPrintable(probe.lines.join(" | ")));
        QCOMPARE(observed.events.back()->type(), QEvent::TouchBegin);
        QCOMPARE(observed.events.back()->points().size(), 1);
        QCOMPARE(observed.events.back()->points().first().id(), 1);
        QTest::touchEvent(&scene.view, device).stationary(0).release(1, onCopy(at).toPoint(), &scene.view);
        QVERIFY(!probe.next("touch-up").isEmpty());
        QCOMPARE(observed.events.back()->type(), QEvent::TouchEnd);
        QVERIFY(!touch.hasActiveContacts());
        const auto onlyRejected = observed.events.size();
        QTest::touchEvent(&scene.view, device).release(0, margin, &scene.view);
        QCOMPARE(observed.events.size(), onlyRejected);
        // A drag keeps its initial picture mapping even after crossing to the other screen.
        QTest::touchEvent(&scene.view, device).press(0, onCopy(at).toPoint(), &scene.view);
        QVERIFY(!probe.next("touch-down").isEmpty());
        QTest::touchEvent(&scene.view, device).move(0, at.toPoint(), &scene.view);
        const auto pinned = observed.events.back()->points().first().scenePosition();
        QVERIFY(QLineF(pinned, at).length() > 100); // No coordinate jump into the other copy.
        QVERIFY(pinned.x() >= source.left() && pinned.x() < source.right());
        QVERIFY(pinned.y() >= source.top() && pinned.y() < source.bottom());
        QVERIFY2(near(probe.next("touch-motion"), client->mapToSurface(client->mapFromScene(pinned))),
                 qPrintable(probe.lines.join(" | ")));
        QVERIFY(touch.cancel());
        QVERIFY(!probe.next("touch-cancel").isEmpty());
        QVERIFY(!touch.hasActiveContacts());
        QVERIFY(touch.setLayout(*replacement)); // The old held contact stays a tombstone.
        const auto afterCancel = observed.events.size();
        QTest::touchEvent(&scene.view, device).move(0, onCopy(to).toPoint(), &scene.view);
        QTest::touchEvent(&scene.view, device).release(0, onCopy(to).toPoint(), &scene.view);
        QCOMPARE(observed.events.size(), afterCancel);
        QVERIFY(touch.setLayout(*layout));
        // Separate fingers may use different physical pictures simultaneously.
        QTest::touchEvent(&scene.view, device).press(0, at.toPoint(), &scene.view)
                                             .press(1, onCopy(to).toPoint(), &scene.view);
        const auto down0 = probe.next("touch-down"); const auto down1 = probe.next("touch-down");
        QVERIFY2(near(down0, {100,80}) && near(down1, {140,110}), qPrintable(probe.lines.join(" | ")));
        QCOMPARE(observed.events.back()->points().size(), 2);
        QCOMPARE(observed.events.back()->points()[0].id(), 0);
        QCOMPARE(observed.events.back()->points()[1].id(), 1);
        QTest::touchEvent(&scene.view, device).stationary(0).move(1, onCopy(at).toPoint(), &scene.view);
        QVERIFY2(near(probe.next("touch-motion"), {100,80}), qPrintable(probe.lines.join(" | ")));
        QVERIFY(QLineF(observed.events.back()->points()[0].scenePosition(), at).length() < 2);
        QVERIFY(QLineF(observed.events.back()->points()[1].sceneLastPosition(), to).length() < 2);
        QTest::touchEvent(&scene.view, device).release(0, at.toPoint(), &scene.view)
                                             .stationary(1);
        QVERIFY(!probe.next("touch-up").isEmpty());
        QVERIFY(touch.hasActiveContacts());
        QTest::touchEvent(&scene.view, device).release(1, onCopy(at).toPoint(), &scene.view);
        QVERIFY(!probe.next("touch-up").isEmpty());
        QVERIFY(!touch.hasActiveContacts());
        // A mouse-only shell control must also work: Quick synthesizes its own
        // mouse event from the mapped touch, rather than the original host touch.
        QQmlComponent buttonComponent(scene.view.engine());
        buttonComponent.setData("import QtQuick\nRectangle { width:100; height:50; color:'silver'; "
            "property int clicks:0; MouseArea { anchors.fill:parent; onClicked:parent.clicks++ } }", QUrl());
        std::unique_ptr<QObject> button(buttonComponent.create());
        QVERIFY2(button, qPrintable(buttonComponent.errorString()));
        auto *buttonItem = qobject_cast<QQuickItem *>(button.get());
        QVERIFY(buttonItem);
        buttonItem->setParentItem(scene.view.rootObject());
        buttonItem->setPosition(source.topLeft() + QPointF(40,50));
        buttonItem->setZ(1000001);
        const auto buttonAt = buttonItem->mapToScene(QPointF(50,25));
        for (bool copied : {false, true}) {
            const auto location = (copied ? onCopy(buttonAt) : buttonAt).toPoint();
            QTest::touchEvent(&scene.view, device).press(0, location, &scene.view);
            QTest::touchEvent(&scene.view, device).release(0, location, &scene.view);
            QTRY_COMPARE(button->property("clicks").toInt(), copied ? 2 : 1);
        }
        // Cancelling a held shell button must release Quick's synthesized mouse
        // grab without clicking it, and a later touch must still work normally.
        QTest::touchEvent(&scene.view, device).press(0, onCopy(buttonAt).toPoint(), &scene.view);
        QVERIFY(touch.cancel());
        QTest::touchEvent(&scene.view, device).release(0, onCopy(buttonAt).toPoint(), &scene.view);
        QCOMPARE(button->property("clicks").toInt(), 2);
        QTest::touchEvent(&scene.view, device).press(0, onCopy(buttonAt).toPoint(), &scene.view);
        QTest::touchEvent(&scene.view, device).release(0, onCopy(buttonAt).toPoint(), &scene.view);
        QTRY_COMPARE(button->property("clicks").toInt(), 3);
        QVERIFY(host->desktopConfinementEngaged()); // Touch never became another pointer owner.
        QVERIFY(!host->engaged());
        QVERIFY(capture(scene.view, rightPrimary ? "mirror-touch-source-right.png" : "mirror-touch-source-left.png"));
        driver.process.write("quit\n");
        QTRY_COMPARE(driver.process.state(), QProcess::NotRunning);
        probe.process.write("quit\n");
        QTRY_COMPARE(probe.process.state(), QProcess::NotRunning);
    }
    void mirrorCopiesClientCursorAndTracksCanonicalPointer_data()
    {
        QTest::addColumn<bool>("rightPrimary");
        QTest::newRow("source left") << false;
        QTest::newRow("source right") << true;
    }
    void mirrorCopiesClientCursorAndTracksCanonicalPointer()
    {
        QFETCH(bool, rightPrimary);
        const auto ipc = qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."), "Requires the owned two-output venue");
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg", {"-s", ipc, QString("[pid=%1] fullscreen enable global")
                                                    .arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(), 0);
        QTRY_COMPARE(scene.view.size(), QSize(2080, 760));
        const QVector<DisplayOutput> outputs{{"HEADLESS-1", {0,0,1120,760}, 1.5, "normal"},
                                             {"HEADLESS-2", {1120,0,960,600}, 2, "90"}};
        const QString primary = rightPrimary ? "HEADLESS-2" : "HEADLESS-1";
        const auto layout = DisplayLayout::create(outputs, primary, DisplayLayout::Mode::Extend);
        const auto mirrored = DisplayLayout::create(outputs, primary, DisplayLayout::Mode::Mirror);
        QVERIFY(layout && mirrored);
        auto &manager = *scene.manager;
        manager.setDisplayLayout(*layout);
        const QRectF domain = rightPrimary ? outputs[1].logical : outputs[0].logical;
        const auto picture = mirrored->mirrorPicture(rightPrimary ? "HEADLESS-1" : "HEADLESS-2");
        QQmlComponent component(scene.view.engine());
        component.setData("import QtQuick\nShaderEffectSource { live:true; recursive:false; smooth:true }", QUrl());
        std::unique_ptr<QObject> mirror(component.create());
        QVERIFY2(mirror, qPrintable(component.errorString()));
        auto *copy = qobject_cast<QQuickItem *>(mirror.get());
        QVERIFY(copy);
        copy->setParentItem(scene.view.contentItem());
        copy->setPosition(picture.topLeft());
        copy->setSize(picture.size());
        copy->setZ(1000000);
        QVERIFY(copy->setProperty("sourceItem", QVariant::fromValue(scene.view.rootObject())));
        QVERIFY(copy->setProperty("sourceRect", domain));
        MirrorCursor cursor(&scene.compositor, &scene.view, scene.view.contentItem());
        cursor.setZ(1000001);
        cursor.setMapping(domain, picture);
        QVERIFY(!cursor.cursorPresented());
        auto *host = manager.findChild<HostPointer *>();
        QVERIFY(host && host->available());
        host->setDesktopRegion(QRegion(domain.toRect()));
        const auto release = qScopeGuard([&] { host->setDesktopRegion({}); });
        LineProcess probe, driver;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        QVERIFY(!probe.next("ready").isEmpty());
        QTRY_VERIFY(!first(manager, "Pointer Probe").isEmpty());
        const auto id = first(manager, "Pointer Probe").value("id").toULongLong();
        manager.moveWindow(id, domain.x() + 250, 160);
        manager.activate(id);
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        driver.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY(!driver.next("driver-ready").isEmpty());
        QPoint current;
        const auto move = [&](QPointF point) {
            current = client->mapToScene(client->mapFromSurface(point)).toPoint();
            return driver.send(QString("abs %1 %2 2080 760").arg(current.x()).arg(current.y()), "ok");
        };
        QVERIFY(move({100,100}));
        QVERIFY(!probe.next("enter").isEmpty());
        QTRY_VERIFY(host->desktopConfinementEngaged());
        QTRY_VERIFY(cursor.cursorPresented());
        QVERIFY(probe.send("cursor show", "cursor-set"));
        QTRY_COMPARE(cursor.presentedCursor().shape(), Qt::BitmapCursor);
        QCOMPARE(cursor.presentedCursor().hotSpot(), QPoint(3,4));
        const qreal scale = picture.width() / domain.width();
        const auto positioned = [&] {
            const auto expected = picture.topLeft() + (QPointF(current) - domain.topLeft() - QPointF(3,4)) * scale;
            const auto bounds = cursor.cursorSceneRect();
            return QLineF(bounds.topLeft(), expected).length() < 1 &&
                qAbs(bounds.width() - 16 * scale) < 0.01 && qAbs(bounds.height() - 16 * scale) < 0.01;
        };
        QTRY_VERIFY(positioned());
        const auto pixel = [&] {
            const auto frame = scene.view.grabWindow();
            if (frame.isNull()) return QColor();
            const auto sample = cursor.cursorSceneRect().center() * frame.devicePixelRatio();
            return frame.pixelColor(sample.toPoint());
        };
        QTRY_COMPARE(pixel(), QColor(Qt::red));
        QVERIFY(move({200,140}));
        QVERIFY(!probe.next("motion").isEmpty());
        QTRY_VERIFY(positioned());
        QTRY_COMPARE(pixel(), QColor(Qt::red));
        // Commit a new cursor buffer without another set_cursor request. Both the native
        // cursor cache and its mirror must redraw, preserving the existing hotspot.
        QVERIFY(probe.send("cursor green", "cursor-updated"));
        QTRY_COMPARE(pixel(), QColor(Qt::green));
        QTRY_VERIFY(positioned());
        QVERIFY(capture(scene.view, rightPrimary ? "mirror-cursor-source-right.png" : "mirror-cursor-source-left.png"));

        QVERIFY(probe.send("lock persistent", "requested"));
        QVERIFY(!probe.next("locked").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured());
        QVERIFY(probe.send("cursor hide", "cursor-set"));
        QTRY_VERIFY(!cursor.cursorPresented());
        QTRY_COMPARE(cursor.presentedCursor().shape(), Qt::BlankCursor);
        QVERIFY(pixel() != QColor(Qt::green));
        pointerReleaseChord(&scene.view);
        QVERIFY(!probe.next("unlocked").isEmpty());
        QTRY_VERIFY(host->desktopConfinementEngaged());
        QVERIFY(probe.send("cursor show", "cursor-set"));
        QTRY_VERIFY(cursor.cursorPresented());
        QTRY_COMPARE(pixel(), QColor(Qt::green));
        // Invalid/non-uniform fit mappings hide the overlay rather than painting an
        // apparently valid cursor at a misleading position.
        cursor.setMapping(domain, QRectF(picture.topLeft(), QSizeF(100,100)));
        QVERIFY(!cursor.cursorPresented());
        cursor.setMapping(domain, picture);
        QTRY_VERIFY(cursor.cursorPresented());
        QTRY_COMPARE(pixel(), QColor(Qt::green));
        QVERIFY(probe.send("drop", "dropped"));
        driver.process.write("quit\n");
        QVERIFY(driver.process.waitForFinished(3000));
        probe.process.write("quit\n");
        QTRY_COMPARE(probe.process.state(), QProcess::NotRunning);
    }

    void mirrorPointerStaysInOneDesktopAndRestoresAfterClientCapture_data()
    {
        QTest::addColumn<QRect>("domain");
        QTest::newRow("primary left") << QRect(0, 0, 1120, 760);
        QTest::newRow("primary right") << QRect(1120, 0, 960, 600);
    }
    void mirrorPointerStaysInOneDesktopAndRestoresAfterClientCapture()
    {
        QFETCH(QRect, domain);
        const auto ipc = qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."), "Requires the owned two-output venue");
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg", {"-s", ipc, QString("[pid=%1] fullscreen enable global")
                                                    .arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(), 0);
        QTRY_COMPARE(scene.view.size(), QSize(2080, 760));
        auto &manager = *scene.manager;
        auto *host = manager.findChild<HostPointer *>();
        QVERIFY(host && host->available());
        // This is the input boundary for one mirrored desktop, not production Mirror wiring.
        // Configure it before the host pointer device exists, so late capability attachment
        // must restore it too. No second constraints object may compete with the game path.
        host->setDesktopRegion(QRegion(domain));
        const auto restore = qScopeGuard([&] { host->setDesktopRegion({}); });
        ObservedHostPointer observed;
        scene.view.installEventFilter(&observed);
        const auto unwatch = qScopeGuard([&] { scene.view.removeEventFilter(&observed); });
        LineProcess probe, driver;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        QVERIFY(!probe.next("ready").isEmpty());
        QTRY_VERIFY(!first(manager, "Pointer Probe").isEmpty());
        const auto id = first(manager, "Pointer Probe").value("id").toULongLong();
        manager.moveWindow(id, domain.x() + 300, 200);
        manager.activate(id);
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        driver.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY(!driver.next("driver-ready").isEmpty());
        const auto moveInside = [&] {
            const auto point = client->mapToScene(client->mapFromSurface({60, 60})).toPoint();
            return driver.send(QString("abs %1 %2 2080 760").arg(point.x()).arg(point.y()), "ok");
        };
        QVERIFY(moveInside());
        QVERIFY(!probe.next("enter").isEmpty());
        QTRY_VERIFY(host->desktopConfinementEngaged());
        QVERIFY(!host->engaged());
        QVERIFY(!manager.pointerCaptured()); // Desktop policy is not an application capture.
        const int otherMonitorX = domain.x() == 0 ? 1400 : 200;
        QVERIFY(!host->confine(QRegion(QRect(otherMonitorX, 100, 100, 100))));
        QVERIFY(host->desktopConfinementEngaged()); // Rejected requests keep the current domain.

        // Native wheel events take the ordinary path with the desktop domain engaged.
        QVERIFY(driver.send("wheel v 1", "ok"));
        QCOMPARE(probe.next("scroll"), QString("scroll none 0.00 15.00"));
        QVERIFY(driver.send("rel 2000 0", "ok"));
        QTRY_VERIFY(observed.position && observed.position->x() > domain.right() - 20 &&
                    observed.position->x() < domain.x() + domain.width());
        QVERIFY(host->desktopConfinementEngaged());
        QVERIFY(moveInside());
        QVERIFY(!probe.next("enter").isEmpty());
        QVERIFY(probe.send("relative", "relative-ready"));

        // Games temporarily own the same host constraint. Full raw deltas survive; the
        // ordinary desktop confinement is suspended, not layered over the client lock.
        QVERIFY(probe.send("lock persistent", "requested"));
        QVERIFY(!probe.next("locked").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured() && host->engaged());
        QVERIFY(!host->desktopConfinementEngaged());
        probe.skip();
        QVERIFY(driver.send("rel 1800 -100", "ok"));
        QCOMPARE(probe.next("relative"), QString("relative 1800.00 -100.00 1800.00 -100.00"));
        pointerReleaseChord(&scene.view);
        QVERIFY(!probe.next("unlocked").isEmpty());
        QTRY_VERIFY(!manager.pointerCaptured() && host->desktopConfinementEngaged());
        QVERIFY(probe.send("drop", "dropped"));

        // A client's smaller confinement replaces desktop policy, then hands it back.
        QVERIFY(moveInside());
        QVERIFY(driver.send("button 272 1", "ok"));
        QVERIFY(driver.send("button 272 0", "ok"));
        QVERIFY(!probe.next("button 272 1").isEmpty());
        QVERIFY(probe.send("confine persistent 20 20 100 80", "requested"));
        QVERIFY2(!probe.next("confined").isEmpty(), qPrintable(probe.lines.join(" | ")));
        QTRY_VERIFY(manager.pointerCaptured() && !host->desktopConfinementEngaged());
        probe.skip();
        QVERIFY(driver.send("rel 2000 0", "ok"));
        QCOMPARE(probe.next("relative"), QString("relative 2000.00 0.00 2000.00 0.00"));
        const auto motion = probe.next("motion").split(' ');
        QVERIFY2(motion.value(1).toDouble() >= 20 && motion.value(1).toDouble() <= 120,
                 qPrintable(motion.join(' ')));
        QVERIFY(probe.send("drop", "dropped"));
        QTRY_VERIFY(!manager.pointerCaptured() && host->desktopConfinementEngaged());

        // Losing the only host pointer while a game is locked must retire the capture before
        // trying to restore desktop policy. A replacement device then restores that policy.
        QVERIFY(probe.send("lock persistent", "requested"));
        QVERIFY(!probe.next("locked").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured() && host->engaged());
        driver.process.write("quit\n");
        QVERIFY(driver.process.waitForFinished(3000));
        QTRY_VERIFY(!manager.pointerCaptured() && !host->desktopConfinementEngaged());
        QVERIFY(probe.send("drop", "dropped"));
        driver.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY(!driver.next("driver-ready").isEmpty());
        QVERIFY(moveInside());
        QTRY_VERIFY(host->desktopConfinementEngaged());
        QVERIFY(!manager.pointerCaptured());

        // Leaving the experiment releases the restriction: the same real host pointer can
        // reach the other monitor again. This is not synthetic QTest mouse positioning.
        host->setDesktopRegion({});
        QTRY_VERIFY(!host->desktopConfinementEngaged());
        QVERIFY(driver.send(QString("abs %1 200 2080 760").arg(otherMonitorX), "ok"));
        QTRY_VERIFY(observed.position && qAbs(observed.position->x() - otherMonitorX) < 2);
        QVERIFY(capture(scene.view, "mirror-pointer-domain-released-" + QString::number(domain.x()) + ".png"));
        driver.process.write("quit\n");
        QVERIFY(driver.process.waitForFinished(3000));
        probe.process.write("quit\n");
        QTRY_COMPARE(probe.process.state(), QProcess::NotRunning);
    }
    void spanningDesktopKeepsNativeApplicationUsableOnSecondScreen_data()
    {
        QTest::addColumn<bool>("mirror");
        QTest::newRow("Extend")<<false;
        QTest::newRow("Mirror-fit")<<true;
    }
    void spanningDesktopKeepsNativeApplicationUsableOnSecondScreen()
    {
        QFETCH(bool,mirror);
        // This is a rendering/input experiment, NOT multi-output protocol support.
        const auto ipc=qEnvironmentVariable("FRANKENSTEIN_HEADLESS_IPC");
        QVERIFY2(ipc.startsWith("/tmp/platinum-headless."),"Requires the owned two-output venue");
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess command;
        command.start("swaymsg", {"-s",ipc,QString("[pid=%1] fullscreen enable global").arg(QCoreApplication::applicationPid())});
        QVERIFY(command.waitForFinished(3000));
        QCOMPARE(command.exitCode(),0);
        QTRY_COMPARE_WITH_TIMEOUT(scene.view.size(),QSize(2080,760),5000);
        const QVector<DisplayOutput> outputs{{"HEADLESS-1",{0,0,1120,760},1.5,"normal"},
                                             {"HEADLESS-2",{1120,0,960,600},2,"90"}};
        const auto layout=DisplayLayout::create(outputs,"HEADLESS-1",mirror?DisplayLayout::Mode::Mirror:DisplayLayout::Mode::Extend);
        QVERIFY(layout);
        scene.manager->setDisplayLayout(*layout);
        std::unique_ptr<QObject> mirrorImage;
        ExperimentalMirrorInput relay;
        relay.view=&scene.view;
        relay.layout=layout;
        if(mirror) {
            // Keep the full host window; only the desktop source becomes 1120x760.
            scene.view.rootObject()->setWidth(1120);
            scene.view.rootObject()->setHeight(760);
            relay.picture=layout->mirrorPicture("HEADLESS-2");
            QQmlComponent component(scene.view.engine());
            component.setData("import QtQuick\nShaderEffectSource { live:true; recursive:false; smooth:true }",QUrl());
            mirrorImage.reset(component.create());
            QVERIFY2(mirrorImage,qPrintable(component.errorString()));
            auto *item=qobject_cast<QQuickItem *>(mirrorImage.get());
            QVERIFY(item);
            item->setParentItem(scene.view.contentItem());
            item->setPosition(relay.picture.topLeft());
            item->setSize(relay.picture.size());
            QVERIFY(item->setProperty("sourceItem",QVariant::fromValue(scene.view.rootObject())));
            scene.view.installEventFilter(&relay);
        }
        auto &manager=*scene.manager;
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"SecondScreen"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY(!first(manager,"Native Probe SecondScreen").isEmpty());
        const auto id=first(manager,"Native Probe SecondScreen").value("id").toULongLong();
        const auto failureState=qScopeGuard([&]{
            if(QTest::currentTestFailed()) {
                qWarning()<<"Multi-display owned probe state"<<QJsonDocument::fromVariant(manager.snapshot()).toJson(QJsonDocument::Compact);
                capture(scene.view,"multi-display-policy-failure.png");
            }
        });
        const int windowX=mirror?150:1250;
        manager.moveWindow(id,windowX,80);
        QTRY_COMPARE(first(manager,"Native Probe SecondScreen").value("x").toInt(),windowX);
        QDBusInterface api("org.frankenstein.NativeProbeSecondScreen","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        LineProcess pointer;
        pointer.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver",{});
        QVERIFY(pointer.process.waitForStarted());
        QVERIFY(!pointer.next("driver-ready").isEmpty());
        QVERIFY(pointer.send("abs 1400 200 2080 760","ok"));
        QVERIFY(pointer.send("button 272 1","ok"));
        QVERIFY(pointer.send("button 272 0","ok"));
        QTRY_VERIFY(QDBusReply<int>(api.call("MousePresses")).value()>0);
        QVERIFY(typeAscii(&scene.view,"second monitor"));
        QTRY_COMPARE(QDBusReply<QString>(api.call("Text")).value(),QString("second monitor"));
        if(mirror) {
            // Read back actual rendered pixels, not just input state. Solid client
            // interior plus several wallpaper patches catch an empty/stale mirror.
            const auto mirroredPixelsMatch=[&]{
                const auto frame=scene.view.grabWindow();
                if(frame.isNull()) return false;
                const double dpr=frame.devicePixelRatio();
                for(const QPointF source : {QPointF(300,180),QPointF(700,450),QPointF(850,350)}) {
                    const QPointF target=relay.picture.topLeft()+QPointF(source.x()*relay.picture.width()/1120,
                                                                        source.y()*relay.picture.height()/760);
                    int error=0;
                    for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                        const auto a=frame.pixelColor(qRound(source.x()*dpr)+dx,qRound(source.y()*dpr)+dy);
                        const auto b=frame.pixelColor(qRound(target.x()*dpr)+dx,qRound(target.y()*dpr)+dy);
                        error+=qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());
                    }
                    if(error>25*3*30) return false;
                }
                return true;
            };
            QTRY_VERIFY(mirroredPixelsMatch());
            const auto presses=QDBusReply<int>(api.call("MousePresses")).value();
            QVERIFY(pointer.send("abs 1130 200 2080 760","ok")); // Left letterbox, no phantom click.
            QVERIFY(pointer.send("button 272 1","ok"));
            QVERIFY(pointer.send("button 272 0","ok"));
            QCOMPARE(QDBusReply<int>(api.call("MousePresses")).value(),presses);
        }
        QVERIFY(capture(scene.view,mirror?"mirror-fit-second-screen.png":"spanning-second-screen.png"));
        if(!mirror) {
            manager.moveWindow(id,950,80); // Same client straddles the scale boundary.
            QTRY_COMPARE(first(manager,"Native Probe SecondScreen").value("x").toInt(),950);
            QVERIFY(capture(scene.view,"spanning-boundary.png"));
            manager.moveWindow(id,1250,80);
            QVERIFY(QDBusReply<bool>(api.call("FullScreenOn","HEADLESS-2")).value());
            QTRY_VERIFY(first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),960);
            QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),600);
            const auto framePosition=[&]{
                const auto currentId=first(manager,"Native Probe SecondScreen").value("id").toULongLong();
                auto *frame=visualItem(scene.view.contentItem(),"frame-"+QString::number(currentId));
                return frame?frame->position():QPointF(-1,-1);
            };
            QTRY_COMPARE(framePosition(),QPointF(1120,0));
            QVERIFY(capture(scene.view,"second-monitor-fullscreen.png"));
            api.call("SetFullScreen",false);
            QTRY_VERIFY(!first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),360);
            QTRY_COMPARE(framePosition(),QPointF(1250,80));
            const auto secondPrimary=DisplayLayout::create(outputs,"HEADLESS-2",DisplayLayout::Mode::Extend);
            QVERIFY(secondPrimary);
            // Normal position and fullscreen content size are different pieces
            // of geometry. A primary switch must keep the actual fullscreen
            // monitor even when combining those values would overlap screen two.
            manager.moveWindow(first(manager,"Native Probe SecondScreen").value("id").toULongLong(),900,80);
            QTRY_COMPARE(framePosition(),QPointF(900,80));
            // With real outputs Qt names a monitor in its fullscreen request.
            // Select screen one explicitly for this primary-switch regression.
            QVERIFY(QDBusReply<bool>(api.call("FullScreenOn","HEADLESS-1")).value());
            QTRY_VERIFY(first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),1120);
            QTRY_COMPARE(framePosition(),QPointF(0,0));
            manager.setDisplayLayout(*secondPrimary);
            QTRY_COMPARE(framePosition(),QPointF(0,0));
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),1120);
            api.call("SetFullScreen",false);
            QTRY_VERIFY(!first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),360);
            auto *bar=visualItem(scene.view.contentItem(),"desktop-menu-bar");
            auto *strip=visualItem(scene.view.contentItem(),"control-strip");
            QVERIFY(bar && strip);
            QTRY_COMPARE(bar->position(),QPointF(1120,0));
            QTRY_COMPARE(bar->width(),qreal(960));
            QTRY_VERIFY(strip->x()>=1120 && strip->x()+strip->width()<=2080);
            QVERIFY(capture(scene.view,"second-primary-desktop.png"));
            manager.moveWindow(first(manager,"Native Probe SecondScreen").value("id").toULongLong(),1250,80);
            QVERIFY(QDBusReply<bool>(api.call("FullScreenOn","HEADLESS-2")).value());
            QTRY_VERIFY(first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),960);
            // A topology replacement simulates removal at the policy boundary;
            // it does not yet prove automatic hotplug detection by the supervisor.
            manager.setDisplayLayout(DisplayLayout::single({1120,760}));
            QTRY_COMPARE(bar->position(),QPointF(0,0));
            QTRY_VERIFY(first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),1120);
            QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),760);
            QTRY_COMPARE(framePosition(),QPointF(0,0));
            api.call("SetFullScreen",false);
            QTRY_VERIFY(!first(manager,"Native Probe SecondScreen").value("fullScreen").toBool());
            QTRY_VERIFY(first(manager,"Native Probe SecondScreen").value("x").toInt()+360<=1120);
        }
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
        if(!mirror) {
            // Real desktop delegate placement over the actual short monitor:
            // y=650 lies below screen two, although inside the spanning canvas.
            manager.setDisplayLayout(*layout);
            auto *items=qobject_cast<DesktopItems *>(manager.desktopItems());
            QVERIFY(items);
            QFile document(items->directory()+"/Across Screens.txt");
            QVERIFY(document.open(QIODevice::WriteOnly));
            document.write("screen layout must not alter these bytes");document.close();
            items->refresh();
            QModelIndex entry;
            for(int row=0;row<items->rowCount();++row)
                if(items->data(items->index(row),DesktopItems::NameRole)==QString("Across Screens.txt")) entry=items->index(row);
            QVERIFY(entry.isValid());
            items->select(items->data(entry,DesktopItems::KeyRole).toString());
            const QPoint start(items->data(entry,DesktopItems::XRole).toInt(),items->data(entry,DesktopItems::YRole).toInt());
            items->beginDrag();items->moveSelected(1300-start.x(),650-start.y());
            items->finishDrag(1400,550);
            auto *icon=visualItem(scene.view.contentItem(),"desktop-entry-Across Screens.txt");
            QTRY_VERIFY((icon=visualItem(scene.view.contentItem(),"desktop-entry-Across Screens.txt")));
            QTRY_VERIFY(icon->x()>=1120 && icon->x()+icon->width()<=2080);
            QTRY_VERIFY(icon->y()>=14 && icon->y()+icon->height()<=600);
            QVERIFY(!items->acceptsPoint(1400,650));
            QVERIFY(capture(scene.view,"secondary-icon-rescue.png"));
            QVERIFY(document.open(QIODevice::ReadOnly));
            QCOMPARE(document.readAll(),QByteArray("screen layout must not alter these bytes"));
        }
        scene.view.removeEventFilter(&relay);
        mirrorImage.reset();
        qInfo()<<"Experimental spanning canvas"<<scene.view.size()<<"DPR"<<scene.view.devicePixelRatio();
    }
#endif
    void initialFullscreenIntentSurvivesTheFirstMappedBuffer()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"InitialFull","--start-fullscreen"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY(first(manager,"Native Probe InitialFull").value("fullScreen").toBool());
        const auto id=first(manager,"Native Probe InitialFull").value("id").toULongLong();
        QDBusInterface api("org.frankenstein.NativeProbeInitialFull","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),scene.view.width());
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),scene.view.height());
        auto *frame=visualItem(scene.view.contentItem(),"frame-"+QString::number(id));
        QVERIFY(frame);
        QTRY_COMPARE(frame->position(),QPointF(0,0));
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
    }
    void browsersReceiveHostClicksAndOpenUsableChildWindows_data()
    {
        QTest::addColumn<bool>("chromium");
        QTest::addColumn<bool>("touch");
        QTest::newRow("Chromium") << true << false;
        QTest::newRow("Firefox") << false << false;
        QTest::newRow("Chromium touch") << true << true;
        QTest::newRow("Firefox touch") << false << true;
    }
    void browsersReceiveHostClicksAndOpenUsableChildWindows()
    {
        QFETCH(bool,chromium);
        QFETCH(bool,touch);
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        const auto profile=scene.files.path()+"/profile";
        const auto home=scene.files.path()+"/home";
        QVERIFY(QDir().mkpath(home));
        if(!chromium) {
            QVERIFY(QDir().mkpath(profile+"/browsers/firefox"));
            QFile preferences(profile+"/browsers/firefox/prefs.js");
            QVERIFY(preferences.open(QIODevice::WriteOnly));
            preferences.write("user_pref(\"browser.aboutwelcome.enabled\",false);\n"
                              "user_pref(\"browser.shell.checkDefaultBrowser\",false);\n"
                              "user_pref(\"browser.startup.homepage_override.mstone\",\"ignore\");\n"
                              "user_pref(\"extensions.enabledScopes\",1);\n"
                              "user_pref(\"extensions.autoDisableScopes\",15);\n"
                              "user_pref(\"datareporting.policy.firstRunURL\",\"\");\n");
        }
        const QByteArray page="<!doctype html><title>Loading host clicks</title>"
            "<div style='height:2400px'></div>"
            "<a href='#clicked' style='position:absolute;left:50px;top:60px;width:150px;height:60px' "
            "onclick=\"document.title='PD Clicked link'\">Search result</a>"
            "<button style='position:absolute;left:300px;top:180px;width:180px;height:60px' "
            "onclick=\"window.open('/popup','OwnedPopup','width=420,height=280')\">Open sign-in shaped window</button>"
            "<script>let clicked=false;const ready=()=>{if(!clicked)document.title='PD Clicks '+innerWidth+' '+innerHeight};"
            "addEventListener('load',ready);addEventListener('resize',ready);"
            "document.querySelector('a').addEventListener('click',()=>clicked=true);"
            "addEventListener('scroll',()=>document.title='PD Scrolled '+scrollY)</script>";
        // "Ready" means painted (two animation frames after load or resize), so the title is not
        // set before the new window has rendered. The test also waits for the window's size to
        // settle before clicking (see below).
        const QByteArray popup="<!doctype html><title>Loading owned popup</title>"
            "<input style='position:absolute;left:50px;top:60px;width:200px;height:40px' "
            "oninput=\"document.title='PD Popup typed '+this.value\">"
            "<script>let inputFocused=false;const input=document.querySelector('input');"
            "input.addEventListener('focus',()=>{inputFocused=true;document.title='PD Popup input focused'});"
            "const ready=()=>requestAnimationFrame(()=>requestAnimationFrame(()=>{"
            "if(!inputFocused)document.title='PD Popup ready '+innerHeight}));"
            "addEventListener('load',ready);addEventListener('resize',ready)</script>";
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost,0));
        connect(&server,&QTcpServer::newConnection,&server,[&] {
            while(auto *socket=server.nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    auto request=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",request);
                    if(!request.contains("\r\n\r\n"))return;
                    const auto body=request.startsWith("GET /popup ")?popup:page;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: "+
                        QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
            }
        });
        frankenstein::applications::LaunchPlan plan;
        plan.program=chromium?"/usr/bin/chromium":"/usr/lib/firefox/firefox";
        plan.arguments=chromium?QStringList{"--no-first-run","--no-default-browser-check","--new-window"}
                               :QStringList{"--new-window"};
        plan.arguments<<QString("http://127.0.0.1:%1/").arg(server.serverPort());
        QString error;
        QVERIFY2(prepareApplicationAppearance(profile,error),qPrintable(error));
        QVERIFY2(prepareStyledApplication(plan,profile,error),qPrintable(error));
        auto environment=nativeApplicationEnvironment(QProcessEnvironment::systemEnvironment(),
            QString::fromUtf8(scene.compositor.socketName()),profile,externalApplicationPlugins());
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        scene.x11.applyTo(environment);
        environment.insert("HOME",home);
        if(!chromium)environment.insert("MOZ_GTK_TITLEBAR_DECORATION","client");
        if(qEnvironmentVariableIsSet("FRANKENSTEIN_FIXTURE_PTRACER")) {
            environment.insert("LD_PRELOAD",qEnvironmentVariable("FRANKENSTEIN_FIXTURE_PTRACER"));
            environment.insert("MOZ_CRASHREPORTER_DISABLE","1");
        }
        silenceFixtureAudio(environment,scene.files.path());
        QProcess browser;
        browser.setProcessEnvironment(environment);
        browser.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+
            (chromium?(touch?"/chromium-touch.txt":"/chromium-host-clicks.txt"):(touch?"/firefox-touch.txt":"/firefox-host-clicks.txt")));
        const auto cleanup=qScopeGuard([&]{if(browser.state()!=QProcess::NotRunning){browser.kill();browser.waitForFinished(3000);}});
        WireTrace wire(scene.compositor);
        const auto failure=qScopeGuard([&]{if(QTest::currentTestFailed()){
            capture(scene.view,chromium?(touch?"native-chromium-touch-failure.png":"native-chromium-host-clicks-failure.png"):(touch?"native-firefox-touch-failure.png":"native-firefox-host-clicks-failure.png"));
            qInfo()<<"Owned browser failure state"<<manager.snapshot();
            if(browser.state()==QProcess::Running)printThreadWaits(browser.processId());
            wire.print();}});
        browser.start(plan.program,plan.arguments);
        QVERIFY(browser.waitForStarted());
        const auto findPrefix=[&](const QString &prefix) {
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("title").toString().startsWith(prefix))return value.toMap();
            return QVariantMap{};
        };
        QVariantMap root;
        QVERIFY2(QTest::qWaitFor([&]{return !(root=findPrefix("PD Clicks ")).isEmpty();},15000),
            "The owned browser did not load the host-click fixture");
        const auto id=root.value("id").toULongLong();
        manager.moveWindow(id,120,60);
        manager.resizeWindow(id,800,500);
        QTRY_VERIFY((root=findPrefix("PD Clicks 800 ")).value("height").toInt()==500);
        auto *client=qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client);
        QVERIFY(QFileInfo(QString("/proc/%1/exe").arg(liveSurfaceClientPid(client->surface()))).canonicalFilePath()
            .endsWith(chromium?"/chromium":"/firefox"));
        LineProcess driver;
        if (!touch) {
            driver.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
            driver.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver");
            QVERIFY(!driver.next("driver-ready").isEmpty());
        }
        auto *touchscreen = touch ? QTest::createTouchDevice() : nullptr;
        const auto click=[&](QWaylandQuickItem *item,QPointF surface) {
            const auto point=item->mapToScene(item->mapFromSurface(surface));
            if (touch) {
                // No pointer hover first: Firefox's independent touch bindings
                // must both receive the gesture, not just its GPU-side binding.
                QTest::touchEvent(&scene.view, touchscreen).press(0, point.toPoint(), &scene.view);
                QTest::qWait(30);
                QTest::touchEvent(&scene.view, touchscreen).release(0, point.toPoint(), &scene.view);
                return true;
            }
            if(!driver.send(QString("abs %1 %2 %3 %4").arg(qRound(point.x())).arg(qRound(point.y()))
                .arg(scene.view.width()).arg(scene.view.height()),"ok"))return false;
            QTest::qWait(50);
            return driver.send("button 272 1","ok") && driver.send("button 272 0","ok");
        };
        const int toolbar=500-root.value("title").toString().section(' ',3,3).toInt();
        QVERIFY(toolbar>=0 && toolbar<300);
        QVERIFY(click(client,{125,qreal(toolbar+90)}));
        QTRY_VERIFY(!findPrefix("PD Clicked link").isEmpty());
        QVERIFY(click(client,{390,qreal(toolbar+210)}));
        // A new Firefox window can still be settling its size when its page reports ready: every
        // failure of this step showed a 420x323 buffer and new size limits around the ready title,
        // then 420x360, so the field's position was computed from a stale height and the click
        // landed above it. Measure only once the window's height and the page's reported inner
        // height have held for 250 ms. The deadline is unchanged.
        QVariantMap child;
        QElapsedTimer steady;
        QVERIFY2(QTest::qWaitFor([&] {
            const auto now=findPrefix("PD Popup ready ");
            if(now.isEmpty())return false;
            if(now.value("height")!=child.value("height") || now.value("title")!=child.value("title")) {
                child=now;
                steady.restart();
                return false;
            }
            return steady.elapsed()>=250;
        },8000),"The owned popup window did not settle");
        const auto childId=child.value("id").toULongLong();
        auto *childClient=qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(childId)));
        QVERIFY(childClient);
        const int childToolbar=child.value("height").toInt()-child.value("title").toString().section(' ',3,3).toInt();
        QVERIFY(click(childClient,{120,qreal(childToolbar+80)}));
        QTRY_VERIFY(childClient->hasActiveFocus());
        // Shell focus is already set at mapping. Wait for the browser/content
        // process to acknowledge this actual input click before injecting a
        // whole hardware-shaped key sequence in one event-loop turn.
        QTRY_VERIFY(!findPrefix("PD Popup input focused").isEmpty());
        QVERIFY(typeAscii(&scene.view,"platinum"));
        QTRY_VERIFY(!findPrefix("PD Popup typed platinum").isEmpty());
        QVERIFY(capture(scene.view,chromium?(touch?"native-chromium-touch-popup.png":"native-chromium-host-popup.png"):(touch?"native-firefox-touch-popup.png":"native-firefox-host-popup.png")));
        manager.closeWindow(childId);
        QTRY_VERIFY(findPrefix("PD Popup").isEmpty());
        if (!touch) {
            // A mouse wheel, as the host delivers one (psn_tch, 2026-10-22: "i cant scroll in chromium"):
            // three notches down over the page, 120 a notch with 15 host pixels each.
            manager.activate(id);
            QTRY_VERIFY(client->hasActiveFocus());
            const auto over = client->mapToScene(client->mapFromSurface({650, qreal(toolbar+260)}));
            for (int notch = 0; notch < 3; ++notch) {
                QWheelEvent wheel(over, scene.view.mapToGlobal(over), QPoint(0, -15), QPoint(0, -120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized);
                QCoreApplication::sendEvent(&scene.view, &wheel);
                QTest::qWait(60);
            }
            // Firefox scrolls smoothly: its first scroll event can still be at the top.
            QTRY_VERIFY2(findPrefix("PD Scrolled ").value("title").toString().section(' ',2,2).toInt() > 0,
                         "Mouse wheel did not scroll the real browser page");
        }
        if (touch) {
            manager.activate(id);
            const auto from = client->mapToScene(client->mapFromSurface({650, qreal(toolbar+260)})).toPoint();
            const auto to = client->mapToScene(client->mapFromSurface({650, qreal(toolbar+80)})).toPoint();
            QTest::touchEvent(&scene.view, touchscreen).press(0, from, &scene.view);
            QTest::qWait(40);
            for (int step = 1; step <= 12; ++step) {
                QTest::touchEvent(&scene.view, touchscreen).move(0, from+(to-from)*step/12, &scene.view);
                QTest::qWait(20);
            }
            QTest::touchEvent(&scene.view, touchscreen).release(0, to, &scene.view);
            QTRY_VERIFY2(!findPrefix("PD Scrolled ").isEmpty(), "Finger swipe did not scroll the real browser page");
            QVERIFY(findPrefix("PD Scrolled ").value("title").toString().section(' ',2,2).toInt() > 0);
            QVERIFY(capture(scene.view, chromium ? "touch-chromium-scrolled.png" : "touch-firefox-scrolled.png"));
        }
        manager.closeWindow(id);
        QTRY_COMPARE(browser.state(),QProcess::NotRunning);
        QCOMPARE(browser.exitStatus(),QProcess::NormalExit);
        QCOMPARE(browser.exitCode(),0);
    }
    void mesaEglAndKittyCompatibilityLaunch()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        const auto evidence=qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        auto environment=scene.childEnvironment();
        environment.remove("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION");
        environment.remove("DISPLAY");
        environment.remove("XAUTHORITY");
        environment.insert("HOME",scene.files.path()+"/home");
        QVERIFY(QDir().mkpath(environment.value("HOME")));
        environment.insert("WAYLAND_DEBUG","client");
        QProcess egl,terminal;
        const auto cleanup=qScopeGuard([&]{
            for(auto *process:{&egl,&terminal})if(process->state()!=QProcess::NotRunning){
                process->kill();process->waitForFinished(3000);
            }
        });
        egl.setProcessEnvironment(environment);
        egl.setStandardOutputFile(evidence+"/native-eglinfo.txt");
        egl.setStandardErrorFile(evidence+"/native-eglinfo-protocol.txt");
        egl.start("/usr/bin/eglinfo",{"-B","-p","wayland"});
        QVERIFY(egl.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(egl.state(),QProcess::NotRunning,15000);
        qInfo()<<"eglinfo exit"<<egl.exitCode()<<bytes(evidence+"/native-eglinfo.txt");
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        scene.x11.applyTo(environment);
        QStringList terminalArguments{"--config","NONE","--title","Platinum GPU Terminal",
            "--override","confirm_os_window_close=0",
            "/usr/bin/python3","-I","-u","-c",
            "from pathlib import Path; print('Platinum terminal ready',flush=True); "
            "Path(__import__('sys').argv[1]).write_text(input()); import time; time.sleep(60)",
            scene.files.path()+"/terminal-input.txt"};
        QString backendError;
        QVERIFY2(configureKnownApplicationBackend("/usr/bin/kitty",terminalArguments,environment,backendError),qPrintable(backendError));
        terminal.setProcessEnvironment(environment);
        terminal.setStandardOutputFile(evidence+"/native-kitty.txt");
        terminal.setStandardErrorFile(evidence+"/native-kitty-protocol.txt");
        terminal.start("/usr/bin/kitty",terminalArguments);
        QVERIFY(terminal.waitForStarted());
        QVERIFY(QTest::qWaitFor([&]{return terminal.state()==QProcess::NotRunning ||
            !first(*scene.manager,"Platinum GPU Terminal").isEmpty();},8000));
        QVERIFY2(!first(*scene.manager,"Platinum GPU Terminal").isEmpty(),
            "Compatibility terminal did not map; inspect native-kitty-protocol.txt");
        QCOMPARE(egl.exitStatus(),QProcess::NormalExit);
        QCOMPARE(egl.exitCode(),0);
        const auto id=first(*scene.manager,"Platinum GPU Terminal").value("id").toULongLong();
        auto *client=qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client);
        QTest::mouseMove(&scene.view,client->mapToScene(QPointF(80,80)).toPoint());
        QTest::mouseClick(&scene.view,Qt::LeftButton,{},client->mapToScene(QPointF(80,80)).toPoint());
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(typeAscii(&scene.view,"platinum"));
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Return,{},28,"\r");
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Return,{},28,"\r");
        QTRY_COMPARE(bytes(scene.files.path()+"/terminal-input.txt"),QByteArray("platinum"));
        QVERIFY(capture(scene.view,"native-kitty-egl.png"));
        scene.manager->closeWindow(first(*scene.manager,"Platinum GPU Terminal").value("id").toULongLong());
        QTRY_COMPARE(terminal.state(),QProcess::NotRunning);
        QCOMPARE(terminal.exitStatus(),QProcess::NormalExit);
        QCOMPARE(terminal.exitCode(),0);
    }
    void gamesLockAndConfineThePointerThroughTheHost()
    {
        // docs/relative-pointer-plan.md: real host input from the owned headless Sway through
        // wlr-virtual-pointer, so relative motion, lock and confinement are the host's own.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QVERIFY2(manager.pointerCaptureAvailable(),
                 "The headless host offers relative-pointer and pointer-constraints; the desktop must use them");
        LineProcess probe, driver;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        QCOMPARE(probe.next("globals"), QString("globals relative=1 constraints=1 idle=1"));
        QVERIFY(!probe.next("ready").isEmpty());
        QVariantMap window;
        QTRY_VERIFY(!(window = first(manager, "Pointer Probe")).isEmpty());
        const auto id = window.value("id").toULongLong();
        manager.moveWindow(id, 300, 200);
        manager.activate(id);
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        // The driver talks to the host display, never the nested one.
        driver.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        driver.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY2(!driver.next("driver-ready").isEmpty(), qPrintable(driver.lines.join(" | ")));
        const auto moveTo = [&](QPointF surface)
        {
            const auto scenePoint = client->mapToScene(client->mapFromSurface(surface)).toPoint();
            return driver.send(QString("abs %1 %2 %3 %4").arg(scenePoint.x()).arg(scenePoint.y())
                                   .arg(scene.view.width()).arg(scene.view.height()), "ok");
        };
        const auto relative = [&](int dx, int dy) { return driver.send(QString("rel %1 %2").arg(dx).arg(dy), "ok"); };
        const auto click = [&]
        { return driver.send("button 272 1", "ok") && driver.send("button 272 0", "ok"); };
        QVERIFY(moveTo({160, 120}));
        const auto enter = probe.next("enter");
        QVERIFY2(!enter.isEmpty(), qPrintable(probe.lines.join(" | ")));
        qInfo().noquote() << "Probe saw" << enter;
        QVERIFY(probe.send("relative", "relative-ready"));

        // Unconstrained: relative and absolute motion both arrive.
        QVERIFY(relative(7, 3));
        QCOMPARE(probe.next("relative"), QString("relative 7.00 3.00 7.00 3.00"));
        QVERIFY(!probe.next("motion").isEmpty());

        // A persistent lock: relative motion continues, absolute motion stops.
        QVERIFY(probe.send("lock persistent", "requested"));
        QVERIFY(!probe.next("locked").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured());
        probe.skip();
        QVERIFY(relative(50, -20));
        QCOMPARE(probe.next("relative"), QString("relative 50.00 -20.00 50.00 -20.00"));
        QVERIFY(relative(400, 0)); // Far past the window edge: still unclipped and still locked.
        QCOMPARE(probe.next("relative"), QString("relative 400.00 0.00 400.00 0.00"));
        QTest::qWait(150);
        QVERIFY2(probe.pending("motion").isEmpty() && probe.pending("leave").isEmpty(),
                 qPrintable(probe.lines.join(" | ")));
        // A locked game usually hides its cursor; the desktop must not keep drawing an arrow.
        QVERIFY(probe.send("cursor hide", "cursor-set"));
        QTRY_COMPARE(client->cursor().shape(), Qt::BlankCursor);
        QVERIFY(capture(scene.view, "native-pointer-locked.png"));
        QVERIFY(probe.send("cursor show", "cursor-set"));
        QTRY_COMPARE(client->cursor().shape(), Qt::BitmapCursor);
        QCOMPARE(client->cursor().hotSpot(), QPoint(3, 4));
        QCOMPARE(client->cursor().pixmap().size(), QSize(16, 16));
        QCOMPARE(client->cursor().pixmap().toImage().pixelColor(8, 8), QColor(Qt::red));

        // The release chord frees it; persistent locks wait for a new click.
        pointerReleaseChord(&scene.view);
        QVERIFY(!probe.next("unlocked").isEmpty());
        QTRY_VERIFY(!manager.pointerCaptured());
        QVERIFY(moveTo({100, 100}));
        QVERIFY(!probe.next("motion").isEmpty());
        QTest::qWait(150);
        QVERIFY(probe.pending("locked").isEmpty());
        QVERIFY(click());
        QVERIFY(!probe.next("button 272 1").isEmpty());
        QVERIFY(!probe.next("locked").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured());

        // Shell menus take the pointer back; a persistent lock returns afterwards.
        manager.beginMenu();
        QVERIFY(manager.menuOpen());
        QVERIFY(!probe.next("unlocked").isEmpty());
        QVERIFY(!manager.pointerCaptured());
        manager.endMenu();
        QVERIFY(!probe.next("locked").isEmpty());

        // A oneshot lock is defunct once deactivated.
        QVERIFY(probe.send("drop", "dropped"));
        QTRY_VERIFY(!manager.pointerCaptured());
        QVERIFY(probe.send("lock oneshot", "requested"));
        QVERIFY(!probe.next("locked").isEmpty());
        manager.beginMenu();
        QVERIFY(!probe.next("unlocked").isEmpty());
        manager.endMenu();
        QTest::qWait(200);
        QVERIFY(probe.pending("locked").isEmpty());
        QVERIFY(!manager.pointerCaptured());
        QVERIFY(probe.send("drop", "dropped"));

        // Confinement keeps the host cursor inside the region; relative motion stays unclipped.
        QVERIFY(moveTo({60, 60}));
        QVERIFY(!probe.next("motion").isEmpty());
        QVERIFY(probe.send("confine persistent 20 20 100 80", "requested"));
        QVERIFY(!probe.next("confined").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured());
        probe.skip();
        QVERIFY(relative(300, 200));
        QCOMPARE(probe.next("relative"), QString("relative 300.00 200.00 300.00 200.00"));
        QTest::qWait(150);
        const auto motions = probe.pending("motion");
        QVERIFY2(!motions.isEmpty(), qPrintable(probe.lines.join(" | ")));
        const auto last = motions.last().split(' ');
        QVERIFY2(last.value(1).toDouble() <= 120 && last.value(2).toDouble() <= 100 &&
                     last.value(1).toDouble() >= 20 && last.value(2).toDouble() >= 20,
                 qPrintable(motions.last()));
        QVERIFY(probe.pending("leave").isEmpty());
        QVERIFY(probe.send("drop", "dropped"));
        QTRY_VERIFY(!manager.pointerCaptured());

        // A second constraint on the same surface is a protocol error that ends the client,
        // and its active lock goes with it.
        QVERIFY(probe.send("lock persistent", "requested"));
        QVERIFY(!probe.next("locked").isEmpty());
        QTRY_VERIFY(manager.pointerCaptured());
        probe.process.write("duplicate\n");
        QCOMPARE(probe.next("protocol-error"), QString("protocol-error zwp_pointer_constraints_v1 1"));
        QTRY_COMPARE(probe.process.state(), QProcess::NotRunning);
        QTRY_VERIFY(!manager.pointerCaptured());
        QTRY_VERIFY(first(manager, "Pointer Probe").isEmpty());
        // The host pointer is free again: absolute motion still works (the client item is gone).
        QVERIFY(driver.send(QString("abs 40 400 %1 %2").arg(scene.view.width()).arg(scene.view.height()), "ok"));
        driver.process.write("quit\n");
        QVERIFY(driver.process.waitForFinished(3000));
    }
    void popupMenusStayOnScreen()
    {
        // Wayland applications cannot see screen positions: the compositor must flip or slide a
        // menu that would cross the screen's edge (xdg-positioner constraint adjustment). Qt
        // 6.12's compositor leaves that to us; without it, menus near an edge were cut off.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup=qScopeGuard([&]{if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}});
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"Edge"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY(!first(manager,"Native Probe Edge").isEmpty());
        QDBusInterface api("org.frankenstein.NativeProbeEdge","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        const auto id=first(manager,"Native Probe Edge").value("id").toULongLong();
        auto *client=visualItem(scene.view.contentItem(),"client-"+QString::number(id));
        QVERIFY(client);
        const auto menuRect=[&]{
            const auto popups=popupItems(client);
            return popups.size()==1 && popups.first()->surface() && popups.first()->surface()->hasContent()
                       ? popups.first()->mapRectToScene(popups.first()->boundingRect()) : QRectF();
        };
        const auto failure=qScopeGuard([&]{if(QTest::currentTestFailed()){
            qWarning()<<"Popup rect"<<menuRect();capture(scene.view,"popup-edge-failure.png");}});
        LineProcess pointer;
        pointer.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        pointer.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver",{});
        QVERIFY(!pointer.next("driver-ready").isEmpty());
        const QRectF screen(0,0,scene.view.width(),scene.view.height());
        // The bottom-right corner: the menu would cross both the right and the bottom edge.
        manager.moveWindow(id,int(screen.width())-370,int(screen.height())-250);
        manager.activate(id);
        QTRY_VERIFY(first(manager,"Native Probe Edge").value("active").toBool());
        const auto window=first(manager,"Native Probe Edge");
        // A real click first: a menu needs a recent input serial, as when a user opens one.
        QVERIFY(pointer.send(QString("abs %1 %2 %3 %4").arg(window.value("x").toInt()+180).arg(window.value("y").toInt()+140)
                                 .arg(scene.view.width()).arg(scene.view.height()),"ok"));
        QVERIFY(pointer.send("button 272 1","ok") && pointer.send("button 272 0","ok"));
        QTRY_VERIFY(QDBusReply<int>(api.call("MousePresses")).value()>0);
        api.call("ShowFileMenuAt",350,220);
        QTRY_VERIFY(!menuRect().isEmpty());
        QTest::qWait(150);
        qInfo()<<"Menu at the screen's corner:"<<menuRect()<<"screen"<<screen;
        QVERIFY2(screen.contains(menuRect()),qPrintable(QString("menu %1,%2 %3x%4 crosses the screen edge")
                     .arg(menuRect().x()).arg(menuRect().y()).arg(menuRect().width()).arg(menuRect().height())));
        // The menu still works where it was moved: it opened from the click point, so its first
        // item is next to the pointer, and Escape closes it.
        QVERIFY(capture(scene.view,"popup-flipped-at-corner.png"));
        api.call("HideFileMenu");
        QTRY_VERIFY(menuRect().isEmpty());
        api.call("Finish");
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
    }
    void touchpadScrollingAndTouchscreenReachApplications()
    {
        // psn_tch's touchpad scrolls with two fingers in small fractional steps, and their panel is a
        // touchscreen. Scrolling is real host input from the owned headless Sway through
        // wlr-virtual-pointer; touches are the desktop window's own touch events.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        LineProcess probe, driver;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        // The desktop announces wl_seat 8 (PointerFrames; Qt 6.12's own is 4); this probe binds its
        // own maximum, 5: frames, sources and stops reach it.
        QCOMPARE(probe.next("seat"), QString("seat 5"));
        QVERIFY(!probe.next("ready").isEmpty());
        QVariantMap window;
        QTRY_VERIFY(!(window = first(manager, "Pointer Probe")).isEmpty());
        const auto id = window.value("id").toULongLong();
        manager.moveWindow(id, 300, 200);
        manager.activate(id);
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        driver.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        driver.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY2(!driver.next("driver-ready").isEmpty(), qPrintable(driver.lines.join(" | ")));
        const auto scenePoint = client->mapToScene(client->mapFromSurface({160, 120})).toPoint();
        QVERIFY(driver.send(QString("abs %1 %2 %3 %4").arg(scenePoint.x()).arg(scenePoint.y())
                                .arg(scene.view.width()).arg(scene.view.height()), "ok"));
        QVERIFY(!probe.next("enter").isEmpty());
        // Host pixels in this client's surface units: the desktop scales client windows.
        const double ratio = (client->mapToSurface(client->mapFromScene(QPointF(0, 100))).y() -
                              client->mapToSurface(client->mapFromScene(QPointF(0, 0))).y()) / 100;
        qInfo("Surface units per host pixel: %.3f", ratio);

        // Five slow two-finger steps of 0.4 pixel. Qt alone turned each one into nothing.
        for (int i = 0; i < 5; ++i)
            QVERIFY(driver.send("finger v 0.4", "ok"));
        const auto scrolled = [&](int column)
        {
            double total = 0;
            for (const auto &line : probe.pending("scroll"))
                total += line.split(' ').value(column).toDouble();
            return total;
        };
        QTRY_VERIFY2(qAbs(scrolled(3) - 2 * ratio) < 0.03, qPrintable(probe.lines.join(" | ")));
        for (const auto &line : probe.pending("scroll"))
            QVERIFY2(line.startsWith("scroll finger 0.00 "), qPrintable(line));
        probe.skip();
        // Lifting the fingers sends a stop (kinetic scrolling may begin) and moves nothing.
        QVERIFY(driver.send("stop v", "ok"));
        QCOMPARE(probe.next("scroll"), QString("scroll finger 0.00 0.00 stop"));
        QTest::qWait(100);
        for (const auto &line : probe.pending("scroll")) // The host may end each axis separately.
            QCOMPARE(line, QString("scroll finger 0.00 0.00 stop"));
        probe.skip();
        QVERIFY(driver.send("finger h 3", "ok"));
        const auto sideways = probe.next("scroll").split(' ');
        QVERIFY2(qAbs(sideways.value(2).toDouble() - 3 * ratio) < 0.02 && sideways.value(3) == "0.00",
                 qPrintable(probe.lines.join(" | ")));
        QVERIFY(driver.send("stop h", "ok"));
        QCOMPARE(probe.next("scroll"), QString("scroll finger 0.00 0.00 stop"));
        QTest::qWait(100);
        for (const auto &line : probe.pending("scroll"))
            QCOMPARE(line, QString("scroll finger 0.00 0.00 stop"));
        probe.skip();
        // A mouse wheel moves 15 units a notch, as the host sends it, with its notch count.
        QVERIFY(driver.send("wheel v 1", "ok"));
        QCOMPARE(probe.next("scroll"), QString("scroll wheel 0.00 15.00 discrete 0 1"));
        QVERIFY(driver.send("wheel v -2", "ok"));
        QCOMPARE(probe.next("scroll"), QString("scroll wheel 0.00 -30.00 discrete 0 -2"));
        QTest::qWait(100);
        QVERIFY2(probe.pending("scroll").isEmpty(), qPrintable(probe.lines.join(" | ")));
        driver.process.write("quit\n");
        QVERIFY(driver.process.waitForFinished(3000));

        // The touchscreen: a press, a drag and a lift arrive where the surface was touched.
        auto *touchscreen = QTest::createTouchDevice();
        const auto touchAt = client->mapToScene(client->mapFromSurface({100, 80})).toPoint();
        const auto dragTo = client->mapToScene(client->mapFromSurface({140, 110})).toPoint();
        const auto near = [](const QString &line, double x, double y)
        {
            const auto parts = line.split(' ');
            return parts.size() == 4 && qAbs(parts[2].toDouble() - x) <= 1 && qAbs(parts[3].toDouble() - y) <= 1;
        };
        QVERIFY(probe.send("touch-extra", "extra-touch-ready"));
        const auto pair = [&](const QString &operation) {
            // Resource iteration order is not protocol order. Each independently
            // bound object must receive exactly one event, whichever comes first.
            const bool got = QTest::qWaitFor([&] {
                return !probe.pending(operation).isEmpty() && !probe.pending("extra-"+operation).isEmpty();
            }, 5000);
            const auto primary = probe.pending(operation), extra = probe.pending("extra-"+operation);
            QStringList result;
            if (got && primary.size()==1 && extra.size()==1) result = {primary.first(), extra.first()};
            probe.skip();
            return result;
        };
        QTest::touchEvent(&scene.view, touchscreen).press(0, touchAt, &scene.view);
        auto received = pair("touch-down");
        QVERIFY2(received.size()==2 && near(received.value(0),100,80) && near(received.value(1),100,80),
                 qPrintable(probe.lines.join(" | ")));
        QTest::touchEvent(&scene.view, touchscreen).move(0, dragTo, &scene.view);
        received = pair("touch-motion");
        QVERIFY2(received.size()==2 && near(received.value(0),140,110) && near(received.value(1),140,110),
                 qPrintable(probe.lines.join(" | ")));
        QTest::touchEvent(&scene.view, touchscreen).release(0, dragTo, &scene.view);
        QVERIFY2(pair("touch-up").size()==2,qPrintable(probe.lines.join(" | ")));
        QTest::touchEvent(&scene.view, touchscreen).press(0, touchAt, &scene.view);
        QVERIFY2(pair("touch-down").size()==2,qPrintable(probe.lines.join(" | ")));
        // Losing the Quick grab must cancel every touch binding, not leave GTK
        // holding a contact after the shell opens a menu or hides a window.
        client->setVisible(false);
        QVERIFY2(pair("touch-cancel").size()==2,qPrintable(probe.lines.join(" | ")));
        client->setVisible(true);
        QTest::touchEvent(&scene.view, touchscreen).release(0, touchAt, &scene.view);
        QVERIFY(probe.send("touch-release-extra", "extra-touch-released"));
        QTest::touchEvent(&scene.view, touchscreen).press(0, touchAt, &scene.view);
        QVERIFY(!probe.next("touch-down").isEmpty());
        QTest::touchEvent(&scene.view, touchscreen).release(0, touchAt, &scene.view);
        QVERIFY(!probe.next("touch-up").isEmpty());
        probe.process.write("quit\n");
        QVERIFY(probe.process.waitForFinished(3000));
    }
    void sdlRelativeMouseModeCapturesThePointer_data()
    {
        QTest::addColumn<QString>("driver");
        QTest::newRow("Wayland") << "wayland";
        QTest::newRow("X11") << "x11";
    }
    void sdlRelativeMouseModeCapturesThePointer()
    {
        // Games ask SDL for relative mouse mode. Over Wayland SDL locks the pointer itself; over
        // X11, Xwayland turns the game's grab into a lock or confinement through the bridge.
        QFETCH(QString, driver);
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QVERIFY(manager.pointerCaptureAvailable());
        if (driver == "x11")
        {
            QVERIFY2(!X11Bridge::helperPath().isEmpty(), "The qualified X11 bridge is part of this build");
            QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(), 10000);
        }
        auto env = scene.childEnvironment();
        env.insert("SDL_VIDEO_DRIVER", driver);
        if (driver == "x11")
            env.remove("WAYLAND_DISPLAY");
        else
            env.remove("DISPLAY");
        LineProcess game, host;
        game.process.setProcessEnvironment(env);
        game.process.start(QCoreApplication::applicationDirPath() + "/relative-mouse-probe");
        QCOMPARE(game.next("ready", 10000), "ready " + driver);
        QVariantMap window;
        QTRY_VERIFY_WITH_TIMEOUT(!(window = first(manager, "Relative Mouse Probe")).isEmpty(), 10000);
        const auto id = window.value("id").toULongLong();
        manager.moveWindow(id, 300, 200);
        manager.activate(id);
        QQuickItem *client = nullptr;
        QTRY_VERIFY((client = visualItem(scene.view.contentItem(), "client-" + QString::number(id))));
        auto *surfaceItem = qobject_cast<QWaylandQuickItem *>(client);
        QVERIFY(surfaceItem);
        QTRY_VERIFY(surfaceItem->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        host.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        host.process.start(QCoreApplication::applicationDirPath() + "/virtual-pointer-driver");
        QVERIFY(!host.next("driver-ready").isEmpty());
        const auto center = surfaceItem->mapToScene(surfaceItem->mapFromSurface(QPointF(160, 120))).toPoint();
        QVERIFY(host.send(QString("abs %1 %2 %3 %4").arg(center.x()).arg(center.y())
                              .arg(scene.view.width()).arg(scene.view.height()), "ok"));
        QVERIFY2(!game.next("motion").isEmpty(), qPrintable(game.lines.join(" | ")));
        QVERIFY(game.send("relative 1", "relative-mode 1"));
        QTRY_VERIFY_WITH_TIMEOUT(manager.pointerCaptured(), 5000);
        game.skip();
        const auto moved = [&](double wantX, double wantY)
        {
            double x = 0, y = 0;
            const bool reached = QTest::qWaitFor([&] {
                x = y = 0;
                for (const auto &line : game.pending("motion"))
                {
                    const auto parts = line.split(' ');
                    x += parts.value(3).toDouble();
                    y += parts.value(4).toDouble();
                }
                return qAbs(x - wantX) < 1 && qAbs(y - wantY) < 1;
            }, 3000);
            if (!reached)
                qInfo() << "Relative motion seen" << x << y << game.pending("motion");
            return reached;
        };
        QVERIFY(host.send("rel 40 10", "ok"));
        QVERIFY(host.send("rel 300 -60", "ok")); // Beyond the window: games keep turning.
        QVERIFY(moved(340, -50));
        QVERIFY(manager.pointerCaptured());
        QVERIFY(capture(scene.view, "native-sdl-relative-" + driver + ".png"));
        pointerReleaseChord(&scene.view);
        QTRY_VERIFY(!manager.pointerCaptured());
        QVERIFY(game.send("relative 0", "relative-mode 0"));
        game.process.write("quit\n");
        QTRY_COMPARE_WITH_TIMEOUT(game.process.state(), QProcess::NotRunning, 5000);
        host.process.write("quit\n");
        QVERIFY(host.process.waitForFinished(3000));
    }
    void notificationsAppearAsPlatinumAlerts()
    {
        // docs/tray-and-notifications.md: libnotify's own client against the desktop's server.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        auto *server = manager.notifications();
        QVERIFY(server);
        auto bus = QDBusConnection::sessionBus();
        QCOMPARE(bus.interface()->serviceOwner("org.freedesktop.Notifications").value(), bus.baseService());
        NotificationSignals signals_;
        QVERIFY(bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                            "org.freedesktop.Notifications", "NotificationClosed", &signals_,
                            SLOT(notificationClosed(uint,uint))));
        QVERIFY(bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                            "org.freedesktop.Notifications", "ActionInvoked", &signals_,
                            SLOT(actionInvoked(uint,QString))));
        QVERIFY(bus.connect("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                            "org.freedesktop.Notifications", "ActivationToken", &signals_,
                            SLOT(activationToken(uint,QString))));
        QDBusInterface api("org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                           "org.freedesktop.Notifications");
        const QDBusReply<QStringList> capabilities = api.call("GetCapabilities");
        QVERIFY(capabilities.isValid() && capabilities.value().contains("actions"));
        QVERIFY(!capabilities.value().contains("body-markup"));
        const auto alerts = [&] { return server->property("alerts").toList(); };
        const auto send = [&](const QStringList &arguments, QString *output = nullptr) {
            QProcess notify;
            notify.setProcessEnvironment(scene.childEnvironment());
            notify.start("/usr/bin/notify-send", arguments);
            // The server lives in this process: keep the event loop running while waiting.
            const bool done = QTest::qWaitFor([&] { return notify.state() == QProcess::NotRunning; }, 5000) &&
                              notify.exitStatus() == QProcess::NormalExit && notify.exitCode() == 0;
            if (output)
                *output = QString::fromUtf8(notify.readAllStandardOutput()).trimmed();
            return done;
        };
        const auto clickItem = [&](const QString &name) {
            if (scene.view.grabWindow().isNull())
                return false;
            auto *item = visualItem(scene.view.contentItem(), name);
            if (!item || !item->isVisible())
                return false;
            item->parentItem()->ensurePolished();
            QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                              item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };

        QString printed;
        QVERIFY(send({"-a", "Probe Mail", "-i", "dialog-information", "-p", "New message",
                      "From the qualification fixture"}, &printed));
        const auto id = printed.toUInt();
        QVERIFY(id > 0);
        QTRY_COMPARE(alerts().size(), 1);
        auto alert = alerts().first().toMap();
        QCOMPARE(alert.value("id").toUInt(), id);
        QCOMPARE(alert.value("application").toString(), QString("Probe Mail"));
        QCOMPARE(alert.value("summary").toString(), QString("New message"));
        QCOMPARE(alert.value("body").toString(), QString("From the qualification fixture"));
        QVERIFY2(alert.value("icon").toString().startsWith("image://platinum/notification/"),
                 qPrintable(alert.value("icon").toString()));
        QQuickItem *frame = nullptr;
        QTRY_VERIFY((frame = visualItem(scene.view.contentItem(), "notification-" + QString::number(id))) &&
                    frame->isVisible());
        const auto corner = frame->mapToScene(QPointF(frame->width(), 0));
        QCOMPARE(qRound(corner.x()), scene.view.width() - 8);
        QVERIFY(corner.y() >= 26);
        QVERIFY(capture(scene.view, "native-notification.png"));

        // Replacing keeps the id and the alert's place.
        QVERIFY(send({"-r", QString::number(id), "-p", "Updated message"}, &printed));
        QCOMPARE(printed.toUInt(), id);
        QTRY_COMPARE(alerts().first().toMap().value("summary").toString(), QString("Updated message"));
        QCOMPARE(alerts().size(), 1);

        // The close box dismisses (reason 2).
        QVERIFY(clickItem("notification-close-" + QString::number(id)));
        QTRY_VERIFY(alerts().isEmpty());
        QTRY_VERIFY(signals_.closed.contains(qMakePair(id, 2u)));

        // An expiry time closes it on its own (reason 1).
        QVERIFY(send({"-t", "300", "-p", "Brief"}, &printed));
        const auto brief = printed.toUInt();
        QTRY_COMPARE(alerts().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(alerts().isEmpty(), 3000);
        QTRY_VERIFY(signals_.closed.contains(qMakePair(brief, 1u)));

        // Actions are buttons; the sender learns which one was chosen.
        QProcess question;
        question.setProcessEnvironment(scene.childEnvironment());
        question.start("/usr/bin/notify-send", {"-A", "reply=Reply", "-A", "later=Later", "Question"});
        QTRY_COMPARE(alerts().size(), 1);
        const auto asked = alerts().first().toMap().value("id").toUInt();
        QCOMPARE(alerts().first().toMap().value("actions").toList().size(), 2);
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "notification-action-later"));
        QVERIFY(capture(scene.view, "native-notification-actions.png"));
        // (QTRY re-evaluates its expression after success, so never wrap a click in it.)
        QVERIFY(clickItem("notification-action-reply"));
        QTRY_COMPARE_WITH_TIMEOUT(question.state(), QProcess::NotRunning, 5000);
        QCOMPARE(QString::fromUtf8(question.readAllStandardOutput()).trimmed(), QString("reply"));
        QVERIFY(signals_.actions.contains(qMakePair(asked, QString("reply"))));
        // Before it, a token lets the application come forward (spec 1.2, xdg-activation).
        QCOMPARE(signals_.order, QStringList({"token", "action"}));
        QCOMPARE(signals_.tokens.size(), 1);
        QCOMPARE(signals_.tokens.first().first, asked);
        QVERIFY(signals_.tokens.first().second.size() >= 32);
        QTRY_VERIFY(alerts().isEmpty());

        // A click on the alert itself is the default action.
        QProcess open;
        open.setProcessEnvironment(scene.childEnvironment());
        open.start("/usr/bin/notify-send", {"-A", "default=Open", "Click me"});
        QTRY_COMPARE(alerts().size(), 1);
        const auto clicked = alerts().first().toMap().value("id").toUInt();
        QVERIFY(alerts().first().toMap().value("actions").toList().isEmpty());
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "notification-body-" + QString::number(clicked)));
        QVERIFY(clickItem("notification-body-" + QString::number(clicked)));
        QTRY_COMPARE_WITH_TIMEOUT(open.state(), QProcess::NotRunning, 5000);
        QCOMPARE(QString::fromUtf8(open.readAllStandardOutput()).trimmed(), QString("default"));

        // The sender may close it (reason 3); at most four alerts are kept.
        for (int i = 0; i < 5; ++i)
            QVERIFY(send({"-p", QString("Alert %1").arg(i)}, &printed));
        QTRY_COMPARE(alerts().size(), 4);
        QCOMPARE(alerts().first().toMap().value("summary").toString(), QString("Alert 1"));
        const auto last = printed.toUInt();
        QVERIFY(api.call("CloseNotification", last).type() == QDBusMessage::ReplyMessage);
        QTRY_VERIFY(signals_.closed.contains(qMakePair(last, 3u)));
        QCOMPARE(alerts().size(), 3);
    }
    void trayItemsBecomeControlStripModules()
    {
        // A libappindicator3 tray application, as Steam and blueman are, on the session bus.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        auto *items = manager.statusItems();
        QVERIFY(items);
        auto env = scene.childEnvironment();
        env.insert("GDK_BACKEND", "wayland");
        // As launched applications get (launch_environment.cpp): the private bus has no dconf,
        // which would otherwise leave a lock directory in the session runtime.
        env.insert("GSETTINGS_BACKEND", "keyfile");
        LineProcess tray;
        tray.process.setProcessEnvironment(env);
        tray.process.start("/usr/bin/python3",
                           {"-I", QFINDTESTDATA("status_item_probe.py")});
        QVERIFY2(!tray.next("ready", 10000).isEmpty(), qPrintable(tray.lines.join(" | ")));
        const auto entry = [&] {
            for (const auto &value : items->property("items").toList())
                if (value.toMap().value("id") == "platinum-status-probe")
                    return value.toMap();
            return QVariantMap{};
        };
        QTRY_VERIFY_WITH_TIMEOUT(!entry().isEmpty(), 10000);
        QCOMPARE(entry().value("title").toString(), QString("Status Probe"));
        QTRY_VERIFY(entry().value("icon").toString().startsWith("image://platinum/status/"));
        QTRY_VERIFY(entry().value("hasMenu").toBool());
        QQuickItem *module = nullptr;
        QTRY_VERIFY((module = visualItem(scene.view.contentItem(), "control-strip-status-platinum-status-probe")) &&
                    module->isVisible());
        const auto clickItem = [&](const QString &name) {
            for (int attempt = 0; attempt < 100; ++attempt)
            {
                if (!scene.view.grabWindow().isNull())
                    if (auto *item = visualItem(scene.view.contentItem(), name); item && item->isVisible())
                    {
                        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
                        return true;
                    }
                QTest::qWait(20);
            }
            return false;
        };
        QVERIFY(clickItem("control-strip-status-platinum-status-probe"));
        auto *menu = visualItem(scene.view.contentItem(), "control-strip-menu");
        QTRY_VERIFY(menu && menu->isVisible());
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "desktop-command-status-item-Show Library"));
        QVERIFY(visualItem(scene.view.contentItem(), "desktop-command-status-open"));
        auto *toggle = visualItem(scene.view.contentItem(), "desktop-command-status-item-Do Not Disturb");
        QVERIFY(toggle && toggle->property("modelData").toMap().value("checked").toBool());
        auto *disabled = visualItem(scene.view.contentItem(), "desktop-command-status-item-Unavailable Item");
        QVERIFY(disabled && disabled->property("modelData").toMap().value("enabled") == false);
        QVERIFY(capture(scene.view, "native-tray-menu.png"));
        QVERIFY(clickItem("desktop-command-status-item-Show Library"));
        QCOMPARE(tray.next("clicked"), QString("clicked Show Library"));
        QTRY_VERIFY(!menu->isVisible());
        // Status changes follow: attention swaps the icon; passive hides the module.
        const auto icon = entry().value("icon").toString();
        QVERIFY(tray.send("attention", "status attention"));
        QTRY_COMPARE(entry().value("status").toString(), QString("NeedsAttention"));
        QTRY_VERIFY(entry().value("icon").toString() != icon);
        QVERIFY(tray.send("passive", "status passive"));
        QTRY_VERIFY(entry().isEmpty());
        QTRY_VERIFY(!visualItem(scene.view.contentItem(), "control-strip-status-platinum-status-probe"));
        QVERIFY(tray.send("active", "status active"));
        QTRY_VERIFY(!entry().isEmpty());
        // Quitting from its own menu removes the module.
        QVERIFY(clickItem("control-strip-status-platinum-status-probe"));
        QVERIFY(clickItem("desktop-command-status-item-Quit Probe"));
        QCOMPARE(tray.next("clicked"), QString("clicked Quit Probe"));
        QTRY_COMPARE_WITH_TIMEOUT(tray.process.state(), QProcess::NotRunning, 5000);
        QTRY_VERIFY(entry().isEmpty());
        QTRY_VERIFY(!visualItem(scene.view.contentItem(), "control-strip-status-platinum-status-probe"));
        QVERIFY(!menu->isVisible());
    }
    void clicksOnAHungApplicationsMenusNeverExhaustTheBus()
    {
        // A tray item and application menu that never answer a click, as a hung application's.
        // Clicks expect no reply: a call awaiting one holds a slot on the bus until the
        // application quits, and on 2026-10-21 the live desktop ran out of them ("maximum number
        // of pending replies ... has been reached"), so its later calls failed.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        auto *items = manager.statusItems();
        auto *broker = qobject_cast<MenuBroker *>(manager.menuBroker());
        QVERIFY(items && broker);
        LineProcess silent;
        silent.process.setProcessEnvironment(scene.childEnvironment());
        silent.process.start("/usr/bin/python3", {"-I", QFINDTESTDATA("silent_menu_probe.py")});
        const auto ready = silent.next("ready", 10000).split(' ');
        QVERIFY2(ready.size() == 3, qPrintable(silent.lines.join(" | ")));
        const auto key = [&] {
            for (const auto &value : items->property("items").toList())
                if (value.toMap().value("id") == "platinum-silent-probe" && value.toMap().value("hasMenu").toBool())
                    return value.toMap().value("key").toString();
            return QString();
        };
        QTRY_VERIFY_WITH_TIMEOUT(!key().isEmpty(), 10000);
        broker->select(1, ready.at(1), "/MenuBar", ready.at(2).toLongLong());
        QTRY_VERIFY_WITH_TIMEOUT(!broker->menus().isEmpty(), 5000);
        // Well past dbus-daemon's built-in 128 pending replies per connection.
        constexpr int clicks = 200;
        for (int i = 0; i < clicks; ++i)
        {
            QVERIFY(QMetaObject::invokeMethod(items, "invoke", Q_ARG(QString, key()), Q_ARG(int, 2)));
            QVERIFY(QMetaObject::invokeMethod(items, "activate", Q_ARG(QString, key())));
            broker->invoke(2, broker->generation(), broker->revision());
        }
        const auto report = [&] {
            silent.process.write("report\n");
            return silent.next("report");
        };
        // Every click arrived, and none awaits an answer.
        const auto expected = QString("report %1 %1 %2 %2").arg(2 * clicks).arg(clicks);
        QString counts;
        (void)QTest::qWaitFor([&] { return (counts = report()) == expected; }, 10000);
        QCOMPARE(counts, expected);
        // The desktop's own calls still work.
        const auto layout = QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(ready.at(1), "/Menu", "com.canonical.dbusmenu", "GetLayout")
                << 0 << -1 << QStringList{},
            QDBus::Block, 3000);
        QVERIFY2(layout.type() == QDBusMessage::ReplyMessage, qPrintable(layout.errorMessage()));
    }
    void idleInhibitorsKeepTheHostAwake()
    {
        // Video and games keep the session from locking: Wayland inhibitors and the session-bus
        // ScreenSaver API both become one inhibitor at the host. Sway's own tree is the witness.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QVERIFY(manager.idleInhibitAvailable());
        const auto host = QFileInfo(qEnvironmentVariable("WAYLAND_DISPLAY")).absoluteDir();
        const auto sockets = host.entryList({"sway-ipc.*.sock"}, QDir::System);
        QCOMPARE(sockets.size(), 1);
        const auto hostInhibits = [&] {
            QProcess tree;
            tree.start("swaymsg", {"-s", host.filePath(sockets.first()), "-t", "get_tree", "-r"});
            if (!tree.waitForFinished(3000))
                return false;
            std::function<bool(const QJsonObject &)> find = [&](const QJsonObject &node) {
                if (node.value("pid").toInteger() == QCoreApplication::applicationPid())
                    return node.value("inhibit_idle").toBool();
                for (const auto *key : {"nodes", "floating_nodes"})
                    for (const auto &child : node.value(key).toArray())
                        if (find(child.toObject()))
                            return true;
                return false;
            };
            return find(QJsonDocument::fromJson(tree.readAllStandardOutput()).object());
        };
        QVERIFY(!manager.idleInhibited());
        QVERIFY(!hostInhibits());
        LineProcess probe;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        QCOMPARE(probe.next("globals"), QString("globals relative=1 constraints=1 idle=1"));
        QVERIFY(!probe.next("ready").isEmpty());
        QVariantMap window;
        QTRY_VERIFY(!(window = first(manager, "Pointer Probe")).isEmpty());
        QVERIFY(probe.send("idle on", "idle-set"));
        QTRY_VERIFY(manager.idleInhibited());
        QTRY_VERIFY(hostInhibits());
        // Only a visible surface inhibits: hiding the application lifts it, showing restores it.
        manager.activate(window.value("id").toULongLong());
        QTRY_COMPARE(manager.activeApplication(), QString("PointerProbe"));
        manager.hideApplication();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.idleInhibited(), 3000);
        QTRY_VERIFY(!hostInhibits());
        manager.showAllApplications();
        QTRY_VERIFY_WITH_TIMEOUT(manager.idleInhibited(), 3000);
        QVERIFY(probe.send("idle off", "idle-set"));
        QTRY_VERIFY(!manager.idleInhibited());
        QTRY_VERIFY(!hostInhibits());
        // The persistent user option is an independent request. Turning it off
        // must not cancel a video's request; ending the video must not cancel it.
        auto *appearance = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(appearance);
        QVERIFY(appearance->setKeepScreenAwake(true));
        QTRY_VERIFY(manager.idleInhibited());
        QTRY_VERIFY(hostInhibits());
        QVERIFY(probe.send("idle on", "idle-set"));
        QVERIFY(appearance->setKeepScreenAwake(false));
        QTRY_VERIFY(manager.idleInhibited());
        QTRY_VERIFY(hostInhibits());
        QVERIFY(appearance->setKeepScreenAwake(true));
        QVERIFY(probe.send("idle off", "idle-set"));
        QTRY_VERIFY(manager.idleInhibited());
        QTRY_VERIFY(hostInhibits());
        QVERIFY(appearance->setKeepScreenAwake(false));
        QTRY_VERIFY(!manager.idleInhibited());
        QTRY_VERIFY(!hostInhibits());
        // A client that disconnects while inhibiting releases it.
        QVERIFY(probe.send("idle on", "idle-set"));
        QTRY_VERIFY(manager.idleInhibited());
        probe.process.kill();
        QTRY_VERIFY(!manager.idleInhibited());
        // The session-bus API, as Chromium, SDL on X11 and Steam use it.
        QDBusInterface saver("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
                             "org.freedesktop.ScreenSaver");
        const QDBusReply<uint> cookie = saver.call("Inhibit", "Qualification", "Playing a video");
        QVERIFY(cookie.isValid() && cookie.value() > 0);
        QTRY_VERIFY(manager.idleInhibited());
        QTRY_VERIFY(hostInhibits());
        QVERIFY(saver.call("UnInhibit", cookie.value()).type() == QDBusMessage::ReplyMessage);
        QTRY_VERIFY(!manager.idleInhibited());
        // A caller that leaves the bus without UnInhibit is forgotten.
        {
            auto other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "idle-client");
            // Asynchronous: the server answers on this thread, so a blocking call here would wait forever.
            auto request = QDBusMessage::createMethodCall("org.freedesktop.ScreenSaver", "/ScreenSaver",
                                                          "org.freedesktop.ScreenSaver", "Inhibit");
            request << QString("Qualification") << QString("Game");
            QDBusPendingReply<uint> held = other.asyncCall(request);
            QTRY_VERIFY(held.isFinished());
            QVERIFY(held.isValid() && held.value() > 0);
            // Another caller cannot end it.
            saver.call("UnInhibit", held.value());
            QTRY_VERIFY(manager.idleInhibited());
        }
        QDBusConnection::disconnectFromBus("idle-client");
        QTRY_VERIFY(!manager.idleInhibited());
        QTRY_VERIFY(!hostInhibits());
    }
    void clientsSeeTheLogicalDesktopSize()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        // Clients are presented one surface unit per desktop unit; the mode and
        // xdg-output logical size both say so, never the host's window pixels.
        const auto pixels=[&]{return scene.view.size();};
        QTRY_COMPARE(scene.compositor.defaultOutput()->currentMode().size(),pixels());
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        probe.setStandardErrorFile(QProcess::nullDevice());
        const auto cleanup=qScopeGuard([&]{
            if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}
        });
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"Geometry"});
        QVERIFY(probe.waitForStarted());
        QDBusInterface api("org.frankenstein.NativeProbeGeometry","/Probe","org.frankenstein.NativeProbe");
        const auto screen=[&]{
            const QDBusReply<int> width=api.call("ScreenWidth"), height=api.call("ScreenHeight");
            return width.isValid() && height.isValid() ? QSize(width.value(),height.value()) : QSize();
        };
        QTRY_VERIFY_WITH_TIMEOUT(api.isValid() && screen().isValid(),8000);
        qInfo()<<"Desktop"<<scene.view.size()<<"ratio"<<scene.view.devicePixelRatio()
               <<"mode"<<scene.compositor.defaultOutput()->currentMode().size()<<"client screen"<<screen();
        QTRY_COMPARE(screen(),scene.view.size());
        QCOMPARE(scene.compositor.defaultOutput()->currentMode().size(),pixels());
        // A nested desktop window can be resized by its host; clients follow.
        scene.view.resize(1000,700);
        QTRY_COMPARE(scene.view.size(),QSize(1000,700));
        QTRY_COMPARE(scene.compositor.defaultOutput()->currentMode().size(),pixels());
        QTRY_COMPARE(screen(),QSize(1000,700));
        QCOMPARE(scene.compositor.defaultOutput()->currentMode().size(),pixels());
        api.call("Finish");
        QTRY_COMPARE_WITH_TIMEOUT(probe.state(),QProcess::NotRunning,5000);
    }
    void inactiveChromeAndApplicationHidingFollowMacOS9()
    {
        // docs/historical-fidelity-review.md F02/F03: an inactive window loses its
        // pinstripes and title-bar boxes; Hide Others and Show All keep the active app.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess probeA, probeB;
        const auto cleanup=qScopeGuard([&]{
            for(auto *process:{&probeA,&probeB})
                if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(3000);}
        });
        for(auto *process:{&probeA,&probeB}) {
            process->setProcessEnvironment(scene.childEnvironment());
            process->start(QCoreApplication::applicationDirPath()+"/native-probe",
                           {process==&probeA ? "ChromeA" : "ChromeB"});
            QVERIFY(process->waitForStarted());
        }
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager,"Native Probe ChromeA").isEmpty() &&
                                 !first(manager,"Native Probe ChromeB").isEmpty(),10000);
        const auto a=first(manager,"Native Probe ChromeA").value("id").toULongLong();
        const auto b=first(manager,"Native Probe ChromeB").value("id").toULongLong();
        const auto shown=[&](const char *kind,qulonglong id) {
            auto *item=visualItem(scene.view.contentItem(),QString(kind)+QString::number(id));
            return item && item->isVisible();
        };
        manager.activate(a);
        QTRY_VERIFY(first(manager,"Native Probe ChromeA").value("active").toBool());
        for(const auto *kind:{"close-box-","zoom-box-","shade-box-"}) {
            QVERIFY2(shown(kind,a),kind);
            QVERIFY2(!shown(kind,b),kind);
        }
        QVERIFY(capture(scene.view,"native-inactive-chrome.png"));
        const auto active=manager.activeApplication();
        manager.hideOtherApplications();
        QTRY_VERIFY(!first(manager,"Native Probe ChromeB").value("visible").toBool());
        QVERIFY(first(manager,"Native Probe ChromeA").value("visible").toBool());
        QCOMPARE(manager.activeApplication(),active);
        manager.showAllApplications();
        QTRY_VERIFY(first(manager,"Native Probe ChromeB").value("visible").toBool());
        QCOMPARE(manager.activeApplication(),active);
        QVERIFY(first(manager,"Native Probe ChromeA").value("active").toBool());
        manager.activate(b);
        QTRY_VERIFY(shown("close-box-",b) && !shown("close-box-",a));
        for(const auto &label:{QString("ChromeA"),QString("ChromeB")})
            QDBusInterface("org.frankenstein.NativeProbe"+label,"/Probe","org.frankenstein.NativeProbe").call("Finish");
        QTRY_COMPARE(probeA.state(),QProcess::NotRunning);
        QTRY_COMPARE(probeB.state(),QProcess::NotRunning);
    }
    void clientFullScreenCoversTheDesktopAndRestores()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        probe.setStandardErrorFile(QProcess::nullDevice());
        const auto cleanup=qScopeGuard([&]{
            if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}
        });
        const auto failureCapture=qScopeGuard([&]{
            if(QTest::currentTestFailed()) capture(scene.view,"native-fullscreen-failure.png");
        });
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"FullScreen"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager,"Native Probe FullScreen").isEmpty(),8000);
        const auto id=first(manager,"Native Probe FullScreen").value("id").toULongLong();
        QDBusInterface api("org.frankenstein.NativeProbeFullScreen","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        auto *frame=visualItem(scene.view.contentItem(),"frame-"+QString::number(id));
        QVERIFY(client && frame);
        auto *xdg=qobject_cast<QWaylandXdgSurface *>(client->shellSurface());
        QVERIFY(xdg && xdg->toplevel());
        manager.moveWindow(id,150,90);
        QTRY_COMPARE(frame->position(),QPointF(150,90));
        const auto normal=xdg->windowGeometry().size();
        const auto origin=client->mapToScene(QPointF(0,0));
        QVERIFY(origin.y()>=26+23); // Below the menu bar and inside the title bar chrome.
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-fullscreen-before.png"));
        const auto isFull=[&]{const QDBusReply<bool> r=api.call("IsFullScreen");return r.isValid() && r.value();};
        // The client asks for full screen; the desktop grants the whole desktop.
        api.call("SetFullScreen",true);
        QTRY_VERIFY(xdg->toplevel()->fullscreen());
        QTRY_VERIFY(isFull());
        QTRY_COMPARE(xdg->windowGeometry().size(),scene.view.size());
        QTRY_COMPARE(client->mapToScene(QPointF(0,0)),QPointF(0,0));
        QVERIFY(frame->z()>visualItem(scene.view.contentItem(),"desktop-menu-bar")->z());
        QVERIFY(xdg->toplevel()->activated());
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-fullscreen.png"));
        // Leaving full screen returns the window, its size and its chrome.
        api.call("SetFullScreen",false);
        QTRY_VERIFY(!xdg->toplevel()->fullscreen());
        QTRY_VERIFY(!isFull());
        QTRY_COMPARE(xdg->windowGeometry().size(),normal);
        QTRY_COMPARE(client->mapToScene(QPointF(0,0)),origin);
        QVERIFY(xdg->toplevel()->activated());
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-fullscreen-restored.png"));
        api.call("Finish");
        QTRY_COMPARE_WITH_TIMEOUT(probe.state(),QProcess::NotRunning,5000);
    }
    void x11ApplicationsAppearAsClassicWindows()
    {
        if (X11Bridge::helperPath().isEmpty()) QSKIP("This build explicitly disables X11 support.");
        NativeScene scene;
        QVERIFY(scene.show());
        QVERIFY2(QTest::qWaitFor([&]{ return scene.x11.ready() || scene.x11.state()=="failed"; },12000),
                 "Private X11 startup timed out");
        QCOMPARE(scene.x11.state(),QString("ready"));
        QProcess denied;
        auto unauthorized=scene.childEnvironment();
        unauthorized.insert("XAUTHORITY","/dev/null");
        denied.setProcessEnvironment(unauthorized);
        denied.start("/usr/bin/xdpyinfo");
        QVERIFY(denied.waitForFinished(3000));
        QVERIFY2(denied.exitCode()!=0,"Private X11 server accepted a client without its cookie");
        auto &manager=*scene.manager;
        // Launch through the production application catalog/environment path.
        // The installed style retains the X11 app's own menu until an XID
        // global-menu association exists; Wayland clients still export menus.
        QVERIFY(QDir().mkpath(scene.files.path()+"/applications"));
        QFile entry(scene.files.path()+"/applications/x11-probe.desktop");
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=X11 Probe\nExec=/usr/bin/env QT_QPA_PLATFORM=xcb " +
            (QCoreApplication::applicationDirPath()+"/native-probe X11").toUtf8()+"\n");
        entry.close();
        auto *catalog=qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(catalog);
        catalog->refresh();
        QTRY_VERIFY(catalog->ready());
        manager.launchApplication("x11-probe.desktop");
        const auto cleanup=qScopeGuard([&]{
            QDBusInterface api("org.frankenstein.NativeProbeX11","/Probe","org.frankenstein.NativeProbe");
            if(api.isValid()) api.call("Finish");
        });
        const auto failureCapture=qScopeGuard([&]{
            if(QTest::currentTestFailed()) capture(scene.view,"native-x11-failure.png");
        });
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager,"Native Probe X11").isEmpty(),10000);
        const auto window=first(manager,"Native Probe X11");
        qInfo()<<"X11 window"<<window;
        const auto id=window.value("id").toULongLong();
        QDBusInterface api("org.frankenstein.NativeProbeX11","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client);
        manager.moveWindow(id,160,110);
        manager.activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-x11-window.png"));
        const auto point=client->mapToScene(client->mapFromSurface(QPointF(120,120))).toPoint();
        QTest::mouseMove(&scene.view,point);
        QTest::qWait(60);
        QTest::mouseClick(&scene.view,Qt::LeftButton,{},point);
        QTRY_VERIFY(QDBusReply<int>(api.call("MousePresses")).value()>0);
        qInfo()<<"X11 click at"<<QDBusReply<int>(api.call("LastX")).value()<<QDBusReply<int>(api.call("LastY")).value();
        QVERIFY(typeAscii(&scene.view,"platinum"));
        QTRY_COMPARE(QDBusReply<QString>(api.call("Text")).value(),QString("platinum"));
        QVERIFY2(QDBusReply<bool>(api.call("LocalMenuVisible")).value(),
                 "X11 application's menu disappeared without a usable global menu");
        api.call("ShowFileMenu");
        QTRY_VERIFY(QDBusReply<bool>(api.call("FileMenuVisible")).value());
        QTest::qWait(100);
        QVERIFY(capture(scene.view,"native-x11-menu.png"));
        QTest::keyClick(&scene.view,Qt::Key_Down);
        QTest::keyClick(&scene.view,Qt::Key_Return);
        QTRY_COMPARE(QDBusReply<int>(api.call("Counter")).value(),1);
        manager.resizeWindow(id,480,300);
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),480);
        QTRY_COMPARE(QDBusReply<int>(api.call("Height")).value(),300);
        manager.shade(id);
        QTRY_VERIFY(first(manager,"Native Probe X11").value("collapsed").toBool());
        manager.shade(id);
        QTRY_VERIFY(!first(manager,"Native Probe X11").value("collapsed").toBool());
        api.call("SetFullScreen",true);
        QTRY_VERIFY(first(manager,"Native Probe X11").value("fullScreen").toBool());
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(),scene.view.width());
        api.call("SetFullScreen",false);
        QTRY_VERIFY(!first(manager,"Native Probe X11").value("fullScreen").toBool());
        // An X11 minimize button (Steam's could not minimize: psn_tch, 2026-10-22) hides the
        // application, as Hide does; activating it shows the window again.
        api.call("Minimize");
        QTRY_VERIFY(!first(manager,"Native Probe X11").value("visible").toBool());
        manager.activate(id);
        QTRY_VERIFY(first(manager,"Native Probe X11").value("visible").toBool());
        api.call("VetoClose",true);
        manager.closeWindow(id);
        QTRY_COMPARE(QDBusReply<int>(api.call("CloseRequests")).value(),1);
        QVERIFY(!first(manager,"Native Probe X11").isEmpty());
        api.call("VetoClose",false);
        manager.closeWindow(id);
        QTRY_VERIFY_WITH_TIMEOUT(first(manager,"Native Probe X11").isEmpty(),5000);
    }
    void applicationMenuRetiresClosedX11GamesButKeepsLiveNativeApps_data()
    {
        QTest::addColumn<bool>("x11");
        QTest::newRow("native Wayland") << false;
        QTest::newRow("shared X11 bridge") << true;
    }
    void applicationMenuRetiresClosedX11GamesButKeepsLiveNativeApps()
    {
        QFETCH(bool, x11);
        NativeScene scene;
        QVERIFY(scene.show());
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(), 10000);
        auto &manager = *scene.manager;
        auto environment = scene.childEnvironment();
        if (x11) environment.insert("QT_QPA_PLATFORM", "xcb");
        QProcess game, survivor;
        game.setProcessEnvironment(environment);
        survivor.setProcessEnvironment(environment);
        const auto cleanup = qScopeGuard([&] {
            for (auto *process : {&game, &survivor})
                if (process->state() != QProcess::NotRunning) {
                    process->kill(); process->waitForFinished(3000);
                }
        });
        const auto contains = [&](qulonglong id) {
            for (const auto &value : manager.applications())
                if (value.toMap().value("id").toULongLong() == id) return true;
            return false;
        };
        const auto application = [&](qulonglong window) {
            return manager.policyState().windows.at({window}).application.value;
        };
        const auto name = [&](qulonglong id) {
            for (const auto &value : manager.applications())
                if (value.toMap().value("id").toULongLong() == id) return value.toMap().value("name").toString();
            return QString();
        };
        game.start(QCoreApplication::applicationDirPath() + "/native-probe",
                   {"GameLifetime", "--app-id=steam_app_999001", "--two-windows"});
        QTRY_VERIFY(!first(manager, "Native Probe GameLifetime").isEmpty());
        const auto gameWindow = first(manager, "Native Probe GameLifetime").value("id").toULongLong();
        const auto gameApplication = application(gameWindow);
        QTRY_VERIFY(!first(manager, "steam_app_999001 second").isEmpty());
        const auto extraWindow = first(manager, "steam_app_999001 second").value("id").toULongLong();
        QCOMPARE(application(extraWindow), gameApplication);
        QTRY_COMPARE(name(gameApplication), QString("Native Probe GameLifetime"));
        survivor.start(QCoreApplication::applicationDirPath() + "/native-probe", {"SurvivorLifetime"});
        QTRY_VERIFY(!first(manager, "Native Probe SurvivorLifetime").isEmpty());
        const auto survivorWindow = first(manager, "Native Probe SurvivorLifetime").value("id").toULongLong();
        const auto survivorApplication = application(survivorWindow);
        QVERIFY(gameApplication != survivorApplication);
        auto *gameItem = qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(), "client-" + QString::number(gameWindow)));
        auto *survivorItem = qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(), "client-" + QString::number(survivorWindow)));
        QVERIFY(gameItem && survivorItem);
        // This is the important distinction: unrelated X11 apps use ONE live
        // Wayland peer, unlike the independently connected native clients.
        QCOMPARE(gameItem->surface()->client() == survivorItem->surface()->client(), x11);
        QCOMPARE(first(manager, "Native Probe GameLifetime").value("viaX11Bridge").toBool(), x11);
        QDBusInterface a("org.frankenstein.NativeProbeGameLifetime", "/Probe", "org.frankenstein.NativeProbe");
        QDBusInterface b("org.frankenstein.NativeProbeSurvivorLifetime", "/Probe", "org.frankenstein.NativeProbe");
        QTRY_VERIFY(a.isValid() && b.isValid());
        manager.closeWindow(extraWindow);
        QTRY_VERIFY(first(manager, "steam_app_999001 second").isEmpty());
        QVERIFY(contains(gameApplication)); // Closing one window is not the last window.
        QCOMPARE(game.state(), QProcess::Running);
        manager.activate(gameWindow);
        a.asyncCall("SetTitle", QString("Fixture Adventure"));
        QTRY_VERIFY(!first(manager, "Fixture Adventure").isEmpty());
        QTRY_COMPARE(name(gameApplication), QString("Fixture Adventure"));
        QTRY_COMPARE(manager.activeApplication(), QString("Fixture Adventure"));
        QVERIFY(capture(scene.view, x11 ? "native-x11-game-named.png" : "native-wayland-game-named.png"));
        a.call("VetoClose", true);
        manager.closeWindow(gameWindow);
        QTRY_COMPARE(QDBusReply<int>(a.call("CloseRequests")).value(), 1);
        QVERIFY(contains(gameApplication)); // A refused close never retires an app.
        a.call("VetoClose", false);
        a.asyncCall("Finish");
        QTRY_COMPARE(game.state(), QProcess::NotRunning);
        QTRY_VERIFY(first(manager, "Fixture Adventure").isEmpty());
        QTRY_VERIFY2(!contains(gameApplication), "Closed game remains in the Application menu while the X11 bridge is alive");
        QVERIFY(contains(survivorApplication));
        QVERIFY(scene.x11.ready());
        b.asyncCall("HideWithoutQuitting");
        QTRY_VERIFY(first(manager, "Native Probe SurvivorLifetime").isEmpty());
        QCOMPARE(survivor.state(), QProcess::Running);
        if (x11) QTRY_VERIFY(!contains(survivorApplication));
        else QVERIFY(contains(survivorApplication)); // Classic windowless native app.
        b.asyncCall("ShowAgain");
        QTRY_VERIFY(!first(manager, "Native Probe SurvivorLifetime").isEmpty());
        const auto restoredWindow = first(manager, "Native Probe SurvivorLifetime").value("id").toULongLong();
        const auto restoredApplication = application(restoredWindow);
        QCOMPARE(restoredApplication == survivorApplication, !x11);
        manager.activate(restoredWindow);
        auto *restored = visualItem(scene.view.contentItem(), "client-" + QString::number(restoredWindow));
        QVERIFY(restored);
        QTRY_VERIFY(restored->hasActiveFocus());
        QVERIFY(typeAscii(&scene.view, "still running"));
        QTRY_COMPARE(QDBusReply<QString>(b.call("Text")).value(), QString("still running"));
        b.asyncCall("Finish");
        QTRY_COMPARE(survivor.state(), QProcess::NotRunning);
        QTRY_VERIFY(!contains(restoredApplication));
        QTRY_COMPARE(manager.applications().size(), 1); // Finder survives.
        QVERIFY(scene.x11.ready());
        QVERIFY(capture(scene.view, x11 ? "native-x11-game-retired.png" : "native-wayland-app-retired.png"));
    }
    void x11OpenGLPresentsFramesAndShutsDownWithTheDesktop()
    {
        if (X11Bridge::helperPath().isEmpty()) QSKIP("This build explicitly disables X11 support.");
        QVERIFY(QFileInfo::exists("/usr/bin/glxgears"));
        QProcess gears;
        QString display;
        const auto cleanup=qScopeGuard([&]{
            if(gears.state()!=QProcess::NotRunning){gears.kill();gears.waitForFinished(3000);}
        });
        {
            NativeScene scene;
            QVERIFY(scene.show());
            QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
            display=scene.x11.display();
            gears.setProcessEnvironment(scene.childEnvironment());
            gears.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/x11-glx-stderr.txt");
            gears.setStandardOutputFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/x11-glx-stdout.txt");
            gears.start("/usr/bin/glxgears",{"-info"});
            QVERIFY(gears.waitForStarted());
            QTRY_VERIFY_WITH_TIMEOUT(!first(*scene.manager,"glxgears").isEmpty(),8000);
            const auto window=first(*scene.manager,"glxgears");
            auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+window.value("id").toString()));
            QVERIFY(client && client->surface());
            QWaylandSurfaceGrabber grabber(client->surface());
            QImage image;
            QObject::connect(&grabber,&QWaylandSurfaceGrabber::success,&grabber,[&](const QImage &value){image=value;});
            QTest::qWait(300);
            grabber.grab();
            QTRY_VERIFY(!image.isNull());
            // An actual GLX frame must contain the red and green gears, not
            // merely a mapped title bar or a black/failed GPU buffer.
            int red=0,green=0;
            const auto colors=image.convertToFormat(QImage::Format_RGB32);
            for(int y=0;y<colors.height();++y)for(int x=0;x<colors.width();++x){
                const auto color=colors.pixel(x,y);
                red+=qRed(color)>80 && qRed(color)>2*qGreen(color) && qRed(color)>2*qBlue(color);
                green+=qGreen(color)>50 && qGreen(color)>2*qRed(color) && qGreen(color)>2*qBlue(color);
            }
            QVERIFY2(red>100 && green>100,"GLX did not present the expected coloured gear pixels");
            QVERIFY(image.save(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/native-x11-glx-buffer.png"));
            QVERIFY(capture(scene.view,"native-x11-glx.png"));
            // Leave the owned X client connected: production teardown must end
            // it after the renderer joins, without a server leak or a hang.
        }
        QTRY_COMPARE_WITH_TIMEOUT(gears.state(),QProcess::NotRunning,5000);
        QVERIFY(!QFileInfo::exists("/tmp/.X11-unix/X"+display.mid(1)));
    }
    void vulkanPresentsRealFramesAndSurvivesWindowChanges_data()
    {
        QTest::addColumn<bool>("x11");
        QTest::addColumn<bool>("displayLoss");
        QTest::addColumn<bool>("preferSdr");
        QTest::addColumn<int>("prefer"); // VkFormat the client asks for first, 0: Qt's default
        QTest::newRow("Wayland") << false << false << false << 0;
        QTest::newRow("X11") << true << false << false << 0;
        QTest::newRow("WaylandDisplayLoss") << false << true << false << 0;
        QTest::newRow("X11DisplayLoss") << true << true << false << 0;
        // Controlled comparison, not a replacement for default-format coverage.
        QTest::newRow("WaylandSdr") << false << false << true << 0;
        QTest::newRow("WaylandSdrDisplayLoss") << false << true << true << 0;
        // RADV's default order sometimes put these first: the compositor imported
        // them, but Qt 6.12's scene graph has no shader for them (blank window).
        QTest::newRow("WaylandPrefersFloat16") << false << false << false << 97; // R16G16B16A16_SFLOAT
        QTest::newRow("WaylandPrefersUnorm16") << false << false << false << 91; // R16G16B16A16_UNORM
    }
    void vulkanPresentsRealFramesAndSurvivesWindowChanges()
    {
        QFETCH(bool,x11);
        QFETCH(bool,displayLoss);
        QFETCH(bool,preferSdr);
        QFETCH(int,prefer);
        if(x11 && X11Bridge::helperPath().isEmpty()) QSKIP("This build explicitly disables X11 support.");
        const auto label=QString(x11 ? "X11" : "Wayland")+(displayLoss ? "Loss" : "")+(preferSdr ? "Sdr" : "")+
                         (prefer ? "Prefers"+QString::number(prefer) : QString());
        const auto evidence=qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        const auto stderrPath=evidence+"/vulkan-"+label+"-stderr.txt";
        QProcess probe;
        const auto expectExit=[&](const QString &stage) {
            const bool exited=QTest::qWaitFor([&]{return probe.state()==QProcess::NotRunning;},5000);
            if(!exited) {
                printThreadWaits(probe.processId());
                if(qEnvironmentVariable("FRANKENSTEIN_VULKAN_DIAGNOSTICS")=="1") {
                    QProcess debugger;
                    auto environment=QProcessEnvironment::systemEnvironment();
                    environment.insert("DEBUGINFOD_URLS","");
                    debugger.setProcessEnvironment(environment);
                    debugger.setStandardOutputFile(evidence+"/vulkan-"+label+"-"+stage+"-stack.txt");
                    debugger.setStandardErrorFile(evidence+"/vulkan-"+label+"-"+stage+"-gdb.txt");
                    debugger.start("gdb",{"-p",QString::number(probe.processId()),"-batch","-nx",
                        "-ex","set debuginfod enabled off","-ex","thread apply all bt 20"});
                    if(!debugger.waitForFinished(3000)){debugger.kill();debugger.waitForFinished(1000);}
                }
            }
            return exited;
        };
        const auto cleanup=qScopeGuard([&]{
            if(probe.state()!=QProcess::NotRunning){probe.kill();probe.waitForFinished(3000);}
        });
        {
            NativeScene scene;
            QVERIFY(scene.show());
            WireTrace wire(scene.compositor);
            const auto failedCapture=qScopeGuard([&]{
                if(QTest::currentTestFailed()) {
                    capture(scene.view,"native-vulkan-"+label+"-failure.png");
                    wire.print();
                }
            });
            if(x11) QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
            auto environment=scene.childEnvironment();
            environment.insert("QT_QPA_PLATFORM",x11 ? "xcb" : "wayland");
            environment.insert("QT_LOGGING_RULES","qt.vulkan=true");
            probe.setProcessEnvironment(environment);
            probe.setStandardErrorFile(stderrPath);
            QStringList probeArguments{label};
            if(preferSdr)probeArguments<<"--sdr";
            if(prefer)probeArguments<<"--prefer="+QString::number(prefer);
            probe.start(QCoreApplication::applicationDirPath()+"/vulkan-probe",probeArguments);
            QVERIFY(probe.waitForStarted());
            // The exact same ASan probe took 22.6 s to create its Vulkan
            // instance on plain owned Sway with the KDE platform theme, before
            // constructing a window (development receipts, 2026-10-21). Its
            // earlier 13.6 s measurement was not a reliable upper bound.
            // Budget instrumented initialization separately; mapping, actual
            // frames, input, resize and teardown retain their own assertions.
#if defined(__SANITIZE_ADDRESS__)
            constexpr int initializationTimeout = 35000;
#else
            constexpr int initializationTimeout = 20000;
#endif
            QTRY_VERIFY_WITH_TIMEOUT(bytes(stderrPath).contains("Startup show returned at"), initializationTimeout);
            const auto title="Vulkan Probe "+label;
            auto &manager=*scene.manager;
            QTRY_VERIFY_WITH_TIMEOUT(!first(manager,title).isEmpty(),8000);
            const auto id=first(manager,title).value("id").toULongLong();
            QPointer<QWaylandQuickShellSurfaceItem> client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
            QVERIFY(client && client->surface());
            const auto api="org.frankenstein.VulkanProbe"+label;
            const auto invoke=[&](const QString &endpoint,const QString &method,const QVariantList &arguments=QVariantList{}) {
                auto message=QDBusMessage::createMethodCall(endpoint,"/Probe","org.frankenstein.VulkanProbe",method);
                message.setArguments(arguments);
                return QDBusConnection::sessionBus().asyncCall(message,2000);
            };
            const auto value=[&](const QString &endpoint,const QString &method) {
                // Rendering/presentation can wait on the compositor. Keep its
                // event loop servicing Wayland while inspecting the GPU client.
                const auto pending=invoke(endpoint,method);
                if(!QTest::qWaitFor([&]{return pending.isFinished();},2000))return QVariant{};
                const auto message=pending.reply();
                return message.type()==QDBusMessage::ReplyMessage ? message.arguments().value(0) : QVariant{};
            };
            QCOMPARE(value(api,"Backend").toString(),QString(x11 ? "xcb" : "wayland"));
            if(x11) {
                // DRI3 1.4 syncobj release points die with Xwayland; the desktop's
                // Xwayland launcher keeps X11 clients on implicit sync instead.
                const auto dri3=value(api,"Dri3Version").toString();
                qInfo()<<"Vulkan"<<label<<"DRI3 offered"<<dri3;
                QVERIFY2(dri3.startsWith("1.") && dri3.mid(2).toInt()<4,
                         qPrintable("Private Xwayland offered DRI3 '"+dri3+"' (syncobj path)"));
            }
            QTRY_VERIFY(value(api,"FrameCount").toInt()>=3);
            if(!x11) {
                // VkFormat 70-97 are 16 bits per channel. The compositor must not
                // advertise formats its scene graph cannot draw.
                const auto chosen=value(api,"ColorFormat").toInt();
                qInfo()<<"Vulkan"<<label<<"colour format"<<chosen;
                QVERIFY2(chosen>0 && (chosen<70 || chosen>97),
                         qPrintable("Wayland client was offered undrawable VkFormat "+QString::number(chosen)));
            }
            qInfo()<<"Vulkan"<<label<<"GPU"<<value(api,"Device").toString()
                   <<"API"<<value(api,"ApiVersion").toString();
            QVERIFY2(client && client->surface(),"Vulkan client disappeared during initialization");
            qInfo()<<"Vulkan buffer"<<client->view()->currentBuffer().bufferType()
                   <<"format"<<client->view()->currentBuffer().bufferFormatEgl()
                   <<"size"<<client->surface()->bufferSize();
            const auto grab=[&] {
                if(!client || !client->surface())return QImage{};
                // Inspect what the user sees. Qt 6.12's direct buffer grabber
                // dereferences a null OpenGL texture for this Vulkan buffer
                // path; the composed desktop readback handles the QSG texture.
                const auto image=scene.view.grabWindow();
                if(image.isNull())return image;
                const auto area=client->mapRectToScene(client->boundingRect());
                const qreal ratio=qreal(image.width())/scene.view.width();
                const auto pixels=QRectF(area.topLeft()*ratio,area.size()*ratio).toAlignedRect();
                return image.copy(pixels.intersected(image.rect()));
            };
            const auto colors=[&](const QImage &image,int phase) {
                if(image.width()<100 || image.height()<100)return false;
                const auto left=image.pixelColor(image.width()/4,image.height()/2);
                const auto right=image.pixelColor(image.width()*3/4,image.height()/2);
                const bool leftOk=phase ? left.red()>100 && left.red()>2*left.green()
                                        : left.green()>100 && left.green()>2*left.red();
                return leftOk && right.blue()>100 && right.blue()>2*right.red() && right.blue()>2*right.green();
            };
            QImage frame;
            QTRY_VERIFY(colors(frame=grab(),0));
            QVERIFY(frame.save(evidence+"/native-vulkan-"+label+"-content.png"));
            QVERIFY(capture(scene.view,"native-vulkan-"+label+".png"));
            if(!displayLoss) {
                const auto before=value(api,"FrameCount").toInt();
                invoke(api,"SetPhase",{1});
                QTRY_VERIFY(colors(frame=grab(),1));
                QVERIFY(value(api,"FrameCount").toInt()>before);
                manager.resizeWindow(id,500,320);
                QTRY_COMPARE(value(api,"Width").toInt(),500);
                QTRY_COMPARE(value(api,"Height").toInt(),320);
                QTRY_VERIFY(value(api,"SwapchainCount").toInt()>=2);
                QTRY_VERIFY(colors(frame=grab(),1));
                invoke(api,"SetFullScreen",{true});
                QTRY_VERIFY(first(manager,title).value("fullScreen").toBool());
                QTRY_COMPARE(value(api,"Width").toInt(),scene.view.width());
                QTRY_COMPARE(value(api,"Height").toInt(),scene.view.height());
                QTRY_VERIFY(colors(frame=grab(),1));
                invoke(api,"SetFullScreen",{false});
                QTRY_VERIFY(!first(manager,title).value("fullScreen").toBool());
                QTRY_COMPARE(value(api,"Width").toInt(),500);
                QTRY_VERIFY(colors(frame=grab(),1));
                manager.closeWindow(id);
                QVERIFY2(expectExit("normal-close"),"Vulkan client did not exit after its close request");
                QCOMPARE(probe.exitStatus(),QProcess::NormalExit);
                QCOMPARE(probe.exitCode(),0);
                QTRY_VERIFY(first(manager,title).isEmpty());
            }
            // Display-loss rows leave this verified, presenting client alive.
            // Normal rows independently require successful window operations
            // and clean close. Each row needs only one Vulkan initialization.
        }
        QVERIFY2(expectExit("retirement"),"Connected Vulkan client remained alive after display teardown");
        const auto errors=bytes(stderrPath);
        QVERIFY2(!errors.contains("AddressSanitizer") && !errors.contains("runtime error:"),qPrintable(stderrPath));
    }
    void clipboardTextTravelsBetweenX11AndWayland()
    {
        if (X11Bridge::helperPath().isEmpty()) QSKIP("This build explicitly disables X11 support.");
        NativeScene scene;
        QVERIFY(scene.show());
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        QProcess x11,wayland;
        const auto cleanup=qScopeGuard([&]{
            for(auto *process:{&x11,&wayland})if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(3000);}
        });
        auto environment=scene.childEnvironment();
        environment.insert("QT_QPA_PLATFORM","xcb");
        environment.insert("QT_PLUGIN_PATH",QCoreApplication::applicationDirPath()+"/plugins");
        environment.insert("QT_STYLE_OVERRIDE","Frankenstein");
        x11.setProcessEnvironment(environment);
        x11.start(QCoreApplication::applicationDirPath()+"/native-probe",{"ClipX"});
        QVERIFY(x11.waitForStarted());
        wayland.setProcessEnvironment(scene.childEnvironment());
        wayland.start(QCoreApplication::applicationDirPath()+"/native-probe",{"ClipW"});
        QVERIFY(wayland.waitForStarted());
        auto &manager=*scene.manager;
        QTRY_VERIFY(!first(manager,"Native Probe ClipX").isEmpty());
        QTRY_VERIFY(!first(manager,"Native Probe ClipW").isEmpty());
        QDBusInterface x("org.frankenstein.NativeProbeClipX","/Probe","org.frankenstein.NativeProbe");
        QDBusInterface w("org.frankenstein.NativeProbeClipW","/Probe","org.frankenstein.NativeProbe");
        QVERIFY(x.isValid() && w.isValid());
        const auto focus=[&](const QString &title) {
            const auto id=first(manager,title).value("id").toULongLong();
            manager.activate(id);
            auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
            if(!client)return false;
            const auto point=client->mapToScene(client->mapFromSurface(QPointF(120,120))).toPoint();
            QTest::mouseMove(&scene.view,point);
            QTest::mouseClick(&scene.view,Qt::LeftButton,{},point);
            return QTest::qWaitFor([&]{return client->hasActiveFocus();},2000);
        };
        const auto paste=[&] {
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Control,Qt::ControlModifier,29);
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_V,Qt::ControlModifier,47);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_V,Qt::ControlModifier,47);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Control,Qt::NoModifier,29);
        };
        const auto text=[&](QDBusInterface &api) {
            // Clipboard reads need the compositor to service the pipe transfer.
            // A blocking inspection call can deadlock against the client's
            // synchronous clipboard read and make a valid transfer time out.
            QDBusPendingReply<QString> reply=api.asyncCall("Text");
            if (!QTest::qWaitFor([&]{return reply.isFinished();},2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        QVERIFY(focus("Native Probe ClipX"));
        x.call("SetClipboard",QString("X11 clipboard → Wayland ✓"));
        QTest::qWait(100);
        QVERIFY(focus("Native Probe ClipW"));
        paste();
        QTRY_COMPARE(text(w),QString("X11 clipboard → Wayland ✓"));
        w.call("SetClipboard",QString("Wayland clipboard → X11 ✓"));
        QTest::qWait(100);
        QVERIFY(focus("Native Probe ClipX"));
        // Command-V reaches the X11 program as Control-V, through the bridge.
        commandKey(&scene.view,Qt::Key_V,47);
        QTRY_COMPARE(text(x),QString("Wayland clipboard → X11 ✓"));
        x.call("Finish");w.call("Finish");
        QTRY_COMPARE(x11.state(),QProcess::NotRunning);
        QTRY_COMPARE(wayland.state(),QProcess::NotRunning);
    }
    void copyAndPasteTravelBetweenRealBrowsers_data()
    {
        QTest::addColumn<bool>("fromChromium");
        QTest::addColumn<QString>("keys"); // control, command, or menu (Edit menu commands)
        QTest::newRow("Chromium to Firefox, Control") << true << "control";
        QTest::newRow("Firefox to Chromium, Control") << false << "control";
        QTest::newRow("Chromium to Firefox, Command") << true << "command";
        QTest::newRow("Firefox to Chromium, Command") << false << "command";
        QTest::newRow("Chromium to Firefox, Edit menu") << true << "menu";
        QTest::newRow("Firefox to Chromium, Edit menu") << false << "menu";
    }
    void copyAndPasteTravelBetweenRealBrowsers()
    {
        QFETCH(bool,fromChromium);
        QFETCH(QString,keys);
        const bool command=keys=="command";
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        const auto profile=scene.files.path()+"/profile";
        const auto home=scene.files.path()+"/home";
        QVERIFY(QDir().mkpath(home));
        QVERIFY(QDir().mkpath(profile+"/browsers/firefox"));
        QFile preferences(profile+"/browsers/firefox/prefs.js");
        QVERIFY(preferences.open(QIODevice::WriteOnly));
        preferences.write("user_pref(\"browser.aboutwelcome.enabled\",false);\n"
                          "user_pref(\"browser.shell.checkDefaultBrowser\",false);\n"
                          "user_pref(\"browser.startup.homepage_override.mstone\",\"ignore\");\n"
                          "user_pref(\"extensions.enabledScopes\",1);\n"
                          "user_pref(\"extensions.autoDisableScopes\",15);\n"
                          "user_pref(\"datareporting.policy.firstRunURL\",\"\");\n");
        preferences.close();
        // One owned page per role. The field fills the window so a click anywhere
        // below the toolbar lands in it; titles report state without reading the
        // clipboard from the test.
        const QString copied=QString::fromUtf8("Platinum clipboard ✓ %1").arg(fromChromium?"chromium":"firefox");
        const QByteArray source="<!doctype html><meta charset=utf-8><title>Loading copy source</title>"
            "<textarea style='position:fixed;left:0;top:0;width:100%;height:100%'>"+copied.toUtf8()+"</textarea>"
            "<script>const t=document.querySelector('textarea');"
            "setInterval(()=>{if(document.hasFocus()&&document.activeElement===t&&document.title==='PD Source ready')document.title='PD Source focused'},50);"
            "addEventListener('copy',()=>document.title='PD Copied '+t.value.substring(t.selectionStart,t.selectionEnd));"
            "addEventListener('load',()=>requestAnimationFrame(()=>requestAnimationFrame(()=>document.title='PD Source ready')))</script>";
        const QByteArray target="<!doctype html><meta charset=utf-8><title>Loading paste target</title>"
            "<textarea style='position:fixed;left:0;top:0;width:100%;height:100%'></textarea>"
            "<script>const t=document.querySelector('textarea');"
            "t.addEventListener('input',()=>document.title='PD Pasted '+t.value);"
            "setInterval(()=>{if(document.hasFocus()&&document.activeElement===t&&document.title==='PD Target ready')document.title='PD Target focused'},50);"
            "addEventListener('load',()=>requestAnimationFrame(()=>requestAnimationFrame(()=>document.title='PD Target ready')))</script>";
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost,0));
        connect(&server,&QTcpServer::newConnection,&server,[&] {
            while(auto *socket=server.nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    auto request=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",request);
                    if(!request.contains("\r\n\r\n"))return;
                    const auto body=request.startsWith("GET /target ")?target:source;
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "+
                        QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
            }
        });
        QString error;
        QVERIFY2(prepareApplicationAppearance(profile,error),qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        QProcess chromium,firefox;
        const auto cleanup=qScopeGuard([&]{for(auto *browser:{&chromium,&firefox})
            if(browser->state()!=QProcess::NotRunning){browser->kill();browser->waitForFinished(3000);}});
        WireTrace wire(scene.compositor);
        const auto failure=qScopeGuard([&]{if(QTest::currentTestFailed()){
            capture(scene.view,QString("native-browser-clipboard-failure-%1.png").arg(QTest::currentDataTag()).replace(' ','-').replace(',',""));
            qInfo()<<"Owned browser clipboard failure state"<<manager.snapshot();
            wire.print();}});
        const auto launch=[&](QProcess &browser,bool isChromium,const QString &path) {
            frankenstein::applications::LaunchPlan plan;
            plan.program=isChromium?"/usr/bin/chromium":"/usr/lib/firefox/firefox";
            plan.arguments=isChromium?QStringList{"--no-first-run","--no-default-browser-check","--new-window"}
                                     :QStringList{"--new-window"};
            plan.arguments<<QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path);
            QString failed;
            if(!prepareStyledApplication(plan,profile,failed)){qWarning().noquote()<<failed;return false;}
            auto environment=nativeApplicationEnvironment(QProcessEnvironment::systemEnvironment(),
                QString::fromUtf8(scene.compositor.socketName()),profile,externalApplicationPlugins());
            scene.x11.applyTo(environment);
            environment.insert("HOME",home);
            if(!isChromium)environment.insert("MOZ_GTK_TITLEBAR_DECORATION","client");
            silenceFixtureAudio(environment,scene.files.path());
            browser.setProcessEnvironment(environment);
            browser.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+
                (isChromium?"/chromium-clipboard.txt":"/firefox-clipboard.txt"));
            browser.start(plan.program,plan.arguments);
            return browser.waitForStarted();
        };
        const auto findPrefix=[&](const QString &prefix) {
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("title").toString().startsWith(prefix))return value.toMap();
            return QVariantMap{};
        };
        auto &from=fromChromium?chromium:firefox;
        auto &to=fromChromium?firefox:chromium;
        QVERIFY(launch(from,fromChromium,"/source"));
        QVERIFY2(QTest::qWaitFor([&]{return !findPrefix("PD Source ready").isEmpty();},20000),
            "The copy source did not load");
        QVERIFY(launch(to,!fromChromium,"/target"));
        QVERIFY2(QTest::qWaitFor([&]{return !findPrefix("PD Target ready").isEmpty();},20000),
            "The paste target did not load");
        const auto sourceId=findPrefix("PD Source ready").value("id").toULongLong();
        const auto targetId=findPrefix("PD Target ready").value("id").toULongLong();
        const auto byId=[&](quint64 id) {
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(value.toMap().value("id").toULongLong()==id)return value.toMap();
            return QVariantMap{};
        };
        // Side by side, never overlapping: each move/resize is re-applied until
        // the window reports it (a just-mapped window can still be placed).
        const auto place=[&](quint64 id,int x) {
            return QTest::qWaitFor([&] {
                const auto now=byId(id);
                if(now.value("x").toInt()==x && now.value("y").toInt()==60 &&
                   now.value("width").toInt()==520 && now.value("height").toInt()==400)return true;
                manager.moveWindow(id,x,60);
                manager.resizeWindow(id,520,400);
                return false;
            },8000);
        };
        QVERIFY2(place(sourceId,20),qPrintable(QString("Source window was not placed: %1").arg(QString::fromUtf8(QJsonDocument::fromVariant(byId(sourceId)).toJson(QJsonDocument::Compact)))));
        QVERIFY2(place(targetId,580),qPrintable(QString("Target window was not placed: %1").arg(QString::fromUtf8(QJsonDocument::fromVariant(byId(targetId)).toJson(QJsonDocument::Compact)))));
        const auto item=[&](quint64 id){return qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(),"client-"+QString::number(id)));};
        QTRY_VERIFY(item(sourceId) && item(targetId));
        QTest::qWait(400);
        LineProcess driver;
        driver.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        driver.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver");
        QVERIFY(!driver.next("driver-ready").isEmpty());
        const auto click=[&](QWaylandQuickItem *client,QPointF surface) {
            const auto point=client->mapToScene(client->mapFromSurface(surface));
            if(!driver.send(QString("abs %1 %2 %3 %4").arg(qRound(point.x())).arg(qRound(point.y()))
                .arg(scene.view.width()).arg(scene.view.height()),"ok"))return false;
            QTest::qWait(50);
            return driver.send("button 272 1","ok") && driver.send("button 272 0","ok");
        };
        const auto chord=[&](Qt::Key key,uint code) {
            const auto modifier=command?Qt::MetaModifier:Qt::ControlModifier;
            const auto modifierKey=command?Qt::Key_Meta:Qt::Key_Control;
            const uint modifierCode=command?125:29;
            nativeKeyEvent(&scene.view,QEvent::KeyPress,modifierKey,modifier,modifierCode);
            nativeKeyEvent(&scene.view,QEvent::KeyPress,key,modifier,code);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,key,modifier,code);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,modifierKey,Qt::NoModifier,modifierCode);
        };
        // Shell items are clicked with the same real pointer.
        const auto clickItem=[&](const QString &name) {
            QQuickItem *target=nullptr;
            if(!QTest::qWaitFor([&]{target=visualItem(scene.view.contentItem(),name);
                return target && target->isVisible();},3000)) {
                qWarning().noquote()<<"Shell item not shown:"<<name<<(target?"(hidden)":"(missing)");
                return false;
            }
            const auto point=target->mapToScene(QPointF(target->width()/2,target->height()/2));
            if(!driver.send(QString("abs %1 %2 %3 %4").arg(qRound(point.x())).arg(qRound(point.y()))
                .arg(scene.view.width()).arg(scene.view.height()),"ok"))return false;
            QTest::qWait(50);
            return driver.send("button 272 1","ok") && driver.send("button 272 0","ok");
        };
        // Edit is the first menu after the Apple menu for applications without menus of their own.
        const auto editMenu=[&](const QString &command) {
            if(!clickItem("bar-entry-1"))return false;
            QTest::qWait(150); // The menu's rows are laid out after it opens.
            return clickItem("desktop-command-"+command);
        };
        // The bottom of each window is page content in both browsers.
        wire.mark("click source");
        const auto field=[&](quint64 id){const auto size=item(id)->surface()->destinationSize();
            return QPointF(size.width()/2.0,size.height()*0.8);};
        manager.activate(sourceId);
        QVERIFY(click(item(sourceId),field(sourceId)));
        QTRY_VERIFY(item(sourceId)->hasActiveFocus());
        QTRY_VERIFY2(!findPrefix("PD Source focused").isEmpty(),"The source field did not take focus");
        QTest::qWait(150);
        wire.mark("select all + copy");
        // Chromium exports its own menus; Firefox gets the desktop's Edit menu.
        if(keys=="menu" && !fromChromium) {
            QVERIFY(editMenu("edit-select-all"));
            QTest::qWait(150);
            QVERIFY(editMenu("edit-copy"));
        } else {
            chord(Qt::Key_A,30);
            QTest::qWait(100);
            chord(Qt::Key_C,46);
        }
        QTRY_VERIFY2(!findPrefix("PD Copied "+copied).isEmpty(),"The source browser did not copy its selection");
        if(keys=="menu" && !fromChromium) {
            // Edit > Show Clipboard shows what was copied, as Mac OS 9 did.
            QVERIFY(editMenu("show-clipboard"));
            QTRY_COMPARE(visualItem(scene.view.contentItem(),"clipboard-description")->property("text").toString(),
                         QString("Clipboard contents: text"));
            QTRY_COMPARE(visualItem(scene.view.contentItem(),"clipboard-text")->property("text").toString(),copied);
            QVERIFY(capture(scene.view,"clipboard-window.png"));
            QVERIFY(clickItem("clipboard-close"));
            QTRY_VERIFY(!visualItem(scene.view.contentItem(),"clipboard-window"));
        }
        QTest::qWait(150);
        wire.mark("click target");
        // As on Mac OS 9, a click on an inactive window only brings it forward.
        manager.activate(targetId);
        QTRY_VERIFY(item(targetId)->hasActiveFocus());
        QVERIFY(click(item(targetId),field(targetId)));
        QTRY_VERIFY(item(targetId)->hasActiveFocus());
        QTRY_VERIFY(!findPrefix("PD Target focused").isEmpty());
        QTest::qWait(150);
        wire.mark("paste");
        if(keys=="menu" && fromChromium)
            QVERIFY(editMenu("edit-paste"));
        else
            chord(Qt::Key_V,47);
        QTRY_VERIFY2_WITH_TIMEOUT(!findPrefix("PD Pasted "+copied).isEmpty(),
            "The target browser did not receive the copied text",5000);
        QVERIFY(capture(scene.view,QString("clipboard-%1-%2.png").arg(fromChromium?"chromium-to-firefox":"firefox-to-chromium",keys)));
        manager.closeWindow(targetId);
        manager.closeWindow(sourceId);
        QTRY_COMPARE_WITH_TIMEOUT(chromium.state(),QProcess::NotRunning,10000);
        QTRY_COMPARE_WITH_TIMEOUT(firefox.state(),QProcess::NotRunning,10000);
    }
    void terminalsCopyAndPasteWithCommandAndKeepControlCForInterrupt()
    {
        if (!QFileInfo::exists("/usr/bin/konsole")) QSKIP("Konsole is not installed.");
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        const auto evidence=qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR");
        const auto pasted=scene.files.path()+"/terminal-pasted.txt";
        const auto interrupted=scene.files.path()+"/terminal-interrupted.txt";
        QProcess probe,terminal;
        const auto cleanup=qScopeGuard([&]{for(auto *process:{&probe,&terminal})
            if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(3000);}});
        probe.setProcessEnvironment(scene.childEnvironment());
        probe.start(QCoreApplication::applicationDirPath()+"/native-probe",{"ClipT"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY(!first(manager,"Native Probe ClipT").isEmpty());
        QDBusInterface api("org.frankenstein.NativeProbeClipT","/Probe","org.frankenstein.NativeProbe");
        QVERIFY(api.isValid());
        auto environment=scene.childEnvironment();
        environment.insert("HOME",scene.files.path()+"/home");
        QVERIFY(QDir().mkpath(environment.value("HOME")));
        terminal.setProcessEnvironment(environment);
        terminal.setStandardOutputFile(evidence+"/konsole-clipboard.txt");
        terminal.setStandardErrorFile(evidence+"/konsole-clipboard-stderr.txt");
        // An owned program: echo one pasted line to a file, then wait to be interrupted.
        terminal.start("/usr/bin/konsole",{"--separate","--nofork","--hide-menubar","--hide-tabbar","-e",
            "/usr/bin/python3","-I","-u","-c",
            "import sys,time; from pathlib import Path; print('PLATINUMMARKER ready',flush=True); "
            "Path(sys.argv[1]).write_text(input())\n"
            "try:\n time.sleep(60)\nexcept KeyboardInterrupt:\n Path(sys.argv[2]).write_text('interrupted'); time.sleep(60)",
            pasted,interrupted});
        QVERIFY(terminal.waitForStarted());
        const auto konsoleWindow=[&] {
            for(const auto &value:manager.snapshot().value("windows").toList())
                if(!value.toMap().value("title").toString().startsWith("Native Probe"))return value.toMap();
            return QVariantMap{};
        };
        QVERIFY2(QTest::qWaitFor([&]{return !konsoleWindow().isEmpty();},15000),"Konsole did not map");
        const auto terminalId=konsoleWindow().value("id").toULongLong();
        const auto probeId=first(manager,"Native Probe ClipT").value("id").toULongLong();
        const auto item=[&](quint64 id){return qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(),"client-"+QString::number(id)));};
        const auto focus=[&](quint64 id){manager.activate(id);
            return QTest::qWaitFor([&]{return item(id) && item(id)->hasActiveFocus();},3000);};
        const auto key=[&](Qt::Key key,Qt::KeyboardModifiers modifiers,uint code,const QString &text={}) {
            nativeKeyEvent(&scene.view,QEvent::KeyPress,key,modifiers,code,text);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,key,modifiers,code,text);
        };
        const auto control=[&](Qt::Key letter,uint code) {
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Control,Qt::ControlModifier,29);
            key(letter,Qt::ControlModifier,code);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Control,Qt::NoModifier,29);
        };
        const auto clipboard=[&] {
            // Reading needs the compositor to service the transfer: never block on it.
            QDBusPendingReply<QString> reply=api.asyncCall("Clipboard");
            if(!QTest::qWaitFor([&]{return reply.isFinished();},2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        WireTrace wire(scene.compositor);
        const auto failure=qScopeGuard([&]{if(QTest::currentTestFailed()){
            capture(scene.view,"native-terminal-clipboard-failure.png");
            qInfo()<<"Terminal clipboard failure state"<<manager.snapshot();
            wire.print();}});
        // Into the terminal: copied by a Qt application, pasted with Command-V.
        QVERIFY(focus(probeId));
        api.call("SetClipboard",QString("platinum terminal paste"));
        QTest::qWait(150);
        QVERIFY(focus(terminalId));
        QTest::qWait(600); // Konsole starts the program after mapping.
        wire.mark("command-v");
        commandKey(&scene.view,Qt::Key_V,47);
        QTest::qWait(150);
        key(Qt::Key_Return,{},28,"\r");
        QTRY_COMPARE_WITH_TIMEOUT(bytes(pasted),QByteArray("platinum terminal paste"),5000);
        // Out of the terminal: select its text with the mouse (Konsole has no
        // Select All shortcut), then Command-C copies it.
        LineProcess driver;
        driver.process.setProcessEnvironment(QProcessEnvironment::systemEnvironment());
        driver.process.start(QCoreApplication::applicationDirPath()+"/virtual-pointer-driver");
        QVERIFY(!driver.next("driver-ready").isEmpty());
        const auto pointTo=[&](QPointF surface) {
            const auto point=item(terminalId)->mapToScene(item(terminalId)->mapFromSurface(surface));
            return driver.send(QString("abs %1 %2 %3 %4").arg(qRound(point.x())).arg(qRound(point.y()))
                .arg(scene.view.width()).arg(scene.view.height()),"ok");
        };
        const auto size=item(terminalId)->surface()->destinationSize();
        wire.mark("drag-select command-c");
        QVERIFY(pointTo({4,qreal(size.height())-12}));
        QTest::qWait(50);
        QVERIFY(driver.send("button 272 1","ok"));
        for(int step=1;step<=8;++step) {
            QVERIFY(pointTo({4+(size.width()-40)*step/8.0,qreal(size.height())-12-(size.height()-110)*step/8.0}));
            QTest::qWait(30);
        }
        QVERIFY(pointTo({4,2})); // Past the top line: the selection clamps to it.
        QTest::qWait(50);
        QVERIFY(driver.send("button 272 0","ok"));
        QTest::qWait(150);
        commandKey(&scene.view,Qt::Key_C,46);
        QTest::qWait(150);
        QVERIFY(focus(probeId));
        QTRY_VERIFY2_WITH_TIMEOUT(clipboard().contains("PLATINUMMARKER ready"),
            qPrintable("Terminal text was not copied: "+clipboard()),5000);
        // Control-C is still the terminal's interrupt, not Copy.
        QVERIFY(focus(terminalId));
        wire.mark("control-c");
        control(Qt::Key_C,46);
        QTRY_COMPARE_WITH_TIMEOUT(bytes(interrupted),QByteArray("interrupted"),5000);
        QVERIFY(clipboard().contains("PLATINUMMARKER ready"));
        QVERIFY(capture(scene.view,"clipboard-terminal.png"));
        api.call("Finish");
        manager.closeWindow(terminalId);
        QTRY_COMPARE_WITH_TIMEOUT(terminal.state(),QProcess::NotRunning,10000);
        QTRY_COMPARE(probe.state(),QProcess::NotRunning);
    }
    void commandChordDoesNotConsumeTypingAfterFocusChanges()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QProcess source, target;
        const auto cleanup = qScopeGuard([&] {
            for (auto *process : {&source, &target})
                if (process->state() != QProcess::NotRunning) {
                    process->kill();
                    process->waitForFinished(3000);
                }
        });
        for (auto [process, name] : {std::pair{&source, "HeldA"}, std::pair{&target, "HeldB"}}) {
            process->setProcessEnvironment(scene.childEnvironment());
            process->start(QCoreApplication::applicationDirPath() + "/native-probe", {name});
            QVERIFY(process->waitForStarted());
        }
        QTRY_VERIFY(!first(manager, "Native Probe HeldA").isEmpty());
        QTRY_VERIFY(!first(manager, "Native Probe HeldB").isEmpty());
        const auto focus = [&](const QString &title) {
            const auto id = first(manager, title).value("id").toULongLong();
            QQuickItem *item = nullptr;
            if (!QTest::qWaitFor([&] {
                    item = visualItem(scene.view.contentItem(), "client-" + QString::number(id));
                    return item != nullptr;
                }, 3000))
                return false;
            manager.activate(id);
            return QTest::qWaitFor([&] { return item && item->hasActiveFocus(); }, 3000);
        };
        QDBusInterface a("org.frankenstein.NativeProbeHeldA", "/Probe", "org.frankenstein.NativeProbe");
        QDBusInterface b("org.frankenstein.NativeProbeHeldB", "/Probe", "org.frankenstein.NativeProbe");
        QVERIFY(a.isValid() && b.isValid());
        QVERIFY(focus("Native Probe HeldA"));
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(a), 3000);
        // A dialog or another application can take focus before the chord's key-up.
        nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Meta, Qt::MetaModifier, 125);
        nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_C, Qt::MetaModifier, 46);
        QVERIFY(focus("Native Probe HeldB"));
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(b), 3000);
        nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_C, Qt::MetaModifier, 46);
        nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Meta, Qt::NoModifier, 125);
        QVERIFY(focus("Native Probe HeldA"));
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(a), 3000);
        QVERIFY(typeAscii(&scene.view, "ccc"));
        const auto text = [&] {
            QDBusPendingReply<QString> reply = a.asyncCall("Text");
            if (!QTest::qWaitFor([&] { return reply.isFinished(); }, 2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        const auto recordInput = qScopeGuard([&] {
            qInfo() << "Held-chord owned client at completion" << QDBusReply<QString>(a.call("InputState")).value();
        });
        QTRY_COMPARE(text(), QString("ccc"));
        a.call("Finish");
        b.call("Finish");
        QTRY_COMPARE(source.state(), QProcess::NotRunning);
        QTRY_COMPARE(target.state(), QProcess::NotRunning);
    }
    void aNewApplicationCopySupersedesADeferredShellCopy()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QProcess source, noClipboard;
        const auto cleanup = qScopeGuard([&] {
            for (auto *process : {&source, &noClipboard})
                if (process->state() != QProcess::NotRunning) {
                    process->kill(); process->waitForFinished(3000);
                }
        });
        source.setProcessEnvironment(scene.childEnvironment());
        source.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Pending"});
        QTRY_VERIFY(!first(manager, "Native Probe Pending").isEmpty());
        noClipboard.setProcessEnvironment(scene.childEnvironment());
        noClipboard.start(QCoreApplication::applicationDirPath() + "/pointer-probe");
        QTRY_VERIFY(!first(manager, "Pointer Probe").isEmpty());
        const auto pointerId = first(manager, "Pointer Probe").value("id").toULongLong();
        manager.activate(pointerId);
        auto *pointerItem = visualItem(scene.view.contentItem(), "client-" + QString::number(pointerId));
        QTRY_VERIFY(pointerItem && pointerItem->hasActiveFocus());
        auto *clipboard = qobject_cast<Clipboard *>(manager.clipboard());
        QVERIFY(clipboard && clipboard->active());
        // The raw pointer fixture never binds wl_data_device: this copy must wait.
        clipboard->copyText("older shell copy");
        QTest::qWait(100);
        QDBusInterface api("org.frankenstein.NativeProbePending", "/Probe", "org.frankenstein.NativeProbe");
        QVERIFY(api.isValid());
        api.call("SetClipboard", QString("newer application copy"));
        QTRY_COMPARE(clipboard->text(), QString("newer application copy"));
        const auto sourceId = first(manager, "Native Probe Pending").value("id").toULongLong();
        manager.activate(sourceId);
        auto *sourceItem = visualItem(scene.view.contentItem(), "client-" + QString::number(sourceId));
        QTRY_VERIFY(sourceItem && sourceItem->hasActiveFocus());
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api), 3000);
        commandKey(&scene.view, Qt::Key_V, 47);
        const auto text = [&] {
            QDBusPendingReply<QString> reply = api.asyncCall("Text");
            if (!QTest::qWaitFor([&] { return reply.isFinished(); }, 2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        QTRY_COMPARE(text(), QString("newer application copy"));
        api.call("Finish");
        QTRY_COMPARE(source.state(), QProcess::NotRunning);
    }
    void clipboardPreviewBoundsImagesWithoutChangingTheCopiedData()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto *clipboard = qobject_cast<Clipboard *>(scene.manager->clipboard());
        QVERIFY(clipboard && clipboard->active());
        const auto png = [](int side) {
            QImage image(side, side, QImage::Format_RGB32);
            image.fill(Qt::blue);
            QByteArray result;
            QBuffer buffer(&result);
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
                return QByteArray();
            return result;
        };
        QMimeData picture;
        const auto small = png(32);
        QVERIFY(!small.isEmpty());
        picture.setData("image/png", small);
        scene.compositor.selectionRetained(&picture);
        QCOMPARE(clipboard->kind(), QString("picture"));
        QVERIFY(clipboard->picture().toString().startsWith("data:image/png;base64,"));
        const auto preview = QByteArray::fromBase64(clipboard->picture().toString().section(',', 1).toLatin1());
        QCOMPARE(QImage::fromData(preview).size(), QSize(32, 32));
        // 4097² RGB pixels exceed the preview budget, despite a tiny PNG encoding.
        const auto large = png(4097);
        QVERIFY(!large.isEmpty() && large.size() < 32 * 1024 * 1024);
        picture.setData("image/png", large);
        picture.setData("text/plain;charset=utf-8", "large image description");
        scene.compositor.selectionRetained(&picture);
        QCOMPARE(clipboard->kind(), QString("text"));
        QCOMPARE(clipboard->text(), QString("large image description"));
        QCOMPARE(picture.data("image/png"), large); // No truncation of actual paste data.
        QCOMPARE(QGuiApplication::clipboard()->mimeData()->data("image/png"), large);
        QMimeData text;
        text.setData("text/plain;charset=utf-8", QByteArray(300 * 1024, 'x'));
        scene.compositor.selectionRetained(&text);
        QCOMPARE(clipboard->text().size(), 256 * 1024);
    }
    void clipboardOutlivesTheApplicationThatCopied()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess source,target;
        const auto cleanup=qScopeGuard([&]{for(auto *process:{&source,&target})
            if(process->state()!=QProcess::NotRunning){process->kill();process->waitForFinished(3000);}});
        for(auto [process,name]:{std::pair{&source,"ClipA"},std::pair{&target,"ClipB"}}) {
            process->setProcessEnvironment(scene.childEnvironment());
            process->start(QCoreApplication::applicationDirPath()+"/native-probe",{name});
            QVERIFY(process->waitForStarted());
        }
        QTRY_VERIFY(!first(manager,"Native Probe ClipA").isEmpty());
        QTRY_VERIFY(!first(manager,"Native Probe ClipB").isEmpty());
        QDBusInterface a("org.frankenstein.NativeProbeClipA","/Probe","org.frankenstein.NativeProbe");
        QDBusInterface b("org.frankenstein.NativeProbeClipB","/Probe","org.frankenstein.NativeProbe");
        QVERIFY(a.isValid() && b.isValid());
        const auto focus=[&](const QString &title){const auto id=first(manager,title).value("id").toULongLong();
            manager.activate(id);
            auto *item=qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
            return QTest::qWaitFor([&]{return item && item->hasActiveFocus();},3000);};
        const auto text=[&] {
            QDBusPendingReply<QString> reply=b.asyncCall("Text");
            if(!QTest::qWaitFor([&]{return reply.isFinished();},2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        QVERIFY(focus("Native Probe ClipA"));
        a.call("SetClipboard",QString::fromUtf8("Kept after Quit ✓"));
        QTest::qWait(300); // The compositor keeps its own copy as soon as it is offered.
        a.call("Finish");
        QTRY_COMPARE(source.state(),QProcess::NotRunning);
        QTRY_VERIFY(first(manager,"Native Probe ClipA").isEmpty());
        QVERIFY(focus("Native Probe ClipB"));
        // The probe's own window activation must arrive before the paste keystroke.
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(b),3000);
        commandKey(&scene.view,Qt::Key_V,47);
        QTRY_COMPARE(text(),QString::fromUtf8("Kept after Quit ✓"));
        b.call("Finish");
        QTRY_COMPARE(target.state(),QProcess::NotRunning);
    }
    void shellTextFieldsShareTheClipboardWithApplications()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess app;
        app.setProcessEnvironment(scene.childEnvironment());
        app.start(QCoreApplication::applicationDirPath()+"/native-probe",{"ClipS"});
        const auto stop=qScopeGuard([&]{if(app.state()!=QProcess::NotRunning){app.kill();app.waitForFinished(3000);}});
        QTRY_VERIFY(!first(manager,"Native Probe ClipS").isEmpty());
        const auto id=first(manager,"Native Probe ClipS").value("id").toULongLong();
        auto *client=visualItem(scene.view.contentItem(),"client-"+QString::number(id));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        QDBusInterface api("org.frankenstein.NativeProbeClipS","/Probe","org.frankenstein.NativeProbe");
        QVERIFY(api.isValid());
        const auto text=[&] {
            QDBusPendingReply<QString> reply=api.asyncCall("Text");
            if(!QTest::qWaitFor([&]{return reply.isFinished();},2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        auto *browser=scene.view.rootObject()->findChild<QObject *>("application-browser");
        QVERIFY(browser);
        // An application's copy pastes into the shell's search field with Command-V.
        api.call("SetClipboard",QString("platinum"));
        QTest::qWait(300);
        altKey(&scene.view,Qt::Key_D,32);
        QTRY_VERIFY(browser->property("opened").toBool());
        auto *search=visualItem(scene.view.contentItem(),"application-search");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        commandKey(&scene.view,Qt::Key_V,47);
        QTRY_COMPARE(search->property("text").toString(),QString("platinum"));
        // The shell's copy pastes into the application.
        commandKey(&scene.view,Qt::Key_A,30);
        QVERIFY(typeAscii(&scene.view,"dreams"));
        commandKey(&scene.view,Qt::Key_A,30);
        commandKey(&scene.view,Qt::Key_C,46);
        QTest::qWait(150);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Escape,{},1);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Escape,{},1);
        QTRY_VERIFY(!browser->property("opened").toBool());
        QTRY_VERIFY(client->hasActiveFocus());
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api),3000);
        commandKey(&scene.view,Qt::Key_V,47);
        QTRY_COMPARE(text(),QString("dreams"));
        api.call("Finish");
        QTRY_COMPARE(app.state(),QProcess::NotRunning);
    }
    void desktopCopyPutsSelectedIconNamesOnTheClipboard()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        auto *items=qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        for(const auto *name:{"Platinum Notes.txt","Dreams.txt"}) {
            QFile file(items->directory()+"/"+name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        items->refresh();
        QProcess app;
        app.setProcessEnvironment(scene.childEnvironment());
        app.start(QCoreApplication::applicationDirPath()+"/native-probe",{"ClipD"});
        const auto stop=qScopeGuard([&]{if(app.state()!=QProcess::NotRunning){app.kill();app.waitForFinished(3000);}});
        QTRY_VERIFY(!first(manager,"Native Probe ClipD").isEmpty());
        const auto id=first(manager,"Native Probe ClipD").value("id").toULongLong();
        auto *client=visualItem(scene.view.contentItem(),"client-"+QString::number(id));
        QVERIFY(client);
        QDBusInterface api("org.frankenstein.NativeProbeClipD","/Probe","org.frankenstein.NativeProbe");
        QVERIFY(api.isValid());
        QVERIFY(manager.activateDesktop());
        QTRY_VERIFY(manager.desktopActive());
        items->clearSelection();
        for(int row=0;row<items->rowCount();++row) {
            const auto name=items->data(items->index(row),DesktopItems::NameRole).toString();
            if(name.endsWith(".txt"))items->select(items->data(items->index(row),DesktopItems::KeyRole).toString(),true);
        }
        QCOMPARE(items->selectionCount(),2);
        const auto expected=items->selectedNames();
        QVERIFY(expected.contains("Platinum Notes.txt") && expected.contains("Dreams.txt") && expected.contains('\n'));
        // Command-C on the desktop copies the names, as the 9.2.2 Finder does.
        commandKey(&scene.view,Qt::Key_C,46);
        manager.activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        // The probe's own window activation must arrive before the paste keystroke.
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api),3000);
        commandKey(&scene.view,Qt::Key_V,47);
        const auto text=[&] {
            QDBusPendingReply<QString> reply=api.asyncCall("Text");
            if(!QTest::qWaitFor([&]{return reply.isFinished();},2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        QTRY_COMPARE(text(),expected);
        api.call("Finish");
        QTRY_COMPARE(app.state(),QProcess::NotRunning);
    }
    void shellMenusCloseOnAKeyLikeMacOS9()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        int deliveredKeys = 0;
        auto *logger = wl_display_add_protocol_logger(scene.compositor.display(),
            [](void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *message) {
                if (direction == WL_PROTOCOL_LOGGER_EVENT &&
                    QByteArrayView(wl_resource_get_class(message->resource)) == "wl_keyboard" &&
                    QByteArrayView(message->message->name) == "key")
                    ++*static_cast<int *>(data);
            }, &deliveredKeys);
        QVERIFY(logger);
        const auto releaseLogger = qScopeGuard([&] { wl_protocol_logger_destroy(logger); });
        auto &manager=*scene.manager;
        QProcess app;
        app.setProcessEnvironment(scene.childEnvironment());
        app.start(QCoreApplication::applicationDirPath()+"/native-probe",{"MenuKeys"});
        const auto stop=qScopeGuard([&]{if(app.state()!=QProcess::NotRunning){app.kill();app.waitForFinished(3000);}});
        QTRY_VERIFY(!first(manager,"Native Probe MenuKeys").isEmpty());
        const auto id=first(manager,"Native Probe MenuKeys").value("id").toULongLong();
        auto *client=visualItem(scene.view.contentItem(),"client-"+QString::number(id));
        QTRY_VERIFY(client && client->hasActiveFocus());
        QDBusInterface api("org.frankenstein.NativeProbeMenuKeys","/Probe","org.frankenstein.NativeProbe");
        QVERIFY(api.isValid());
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api), 3000);
        auto *root=scene.view.rootObject();
        // The Apple menu is the desktop's own; a key closes it and reaches nothing else.
        auto *apple=visualItem(scene.view.contentItem(),"bar-entry-0");
        QVERIFY(apple);
        const auto at=apple->mapToScene(QPointF(apple->width()/2,apple->height()/2)).toPoint();
        QTest::mouseClick(&scene.view,Qt::LeftButton,{},at);
        QTRY_COMPARE(root->property("openMenu").toInt(),0);
        QVERIFY(manager.menuOpen());
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Down,{},108);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Down,{},108);
        QTRY_COMPARE(root->property("openMenu").toInt(),-1);
        QTRY_VERIFY(!manager.menuOpen());
        // A modifier alone leaves it open.
        QTest::mouseClick(&scene.view,Qt::LeftButton,{},at);
        QTRY_COMPARE(root->property("openMenu").toInt(),0);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Shift,Qt::ShiftModifier,42);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Shift,{},42);
        QTest::qWait(100);
        QCOMPARE(root->property("openMenu").toInt(),0);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Escape,{},1);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Escape,{},1);
        QTRY_COMPARE(root->property("openMenu").toInt(),-1);
        // Typing then reaches the application again; the closing key did not.
        QTRY_VERIFY(client->hasActiveFocus());
        const auto inputState = [&] {
            return QVariantMap{{"itemFocus", client->hasActiveFocus()},
                               {"inputEnabled", client->property("inputEventsEnabled")},
                               {"seatFocus", scene.compositor.defaultSeat()->keyboardFocus() ==
                                   qobject_cast<QWaylandQuickItem *>(client)->surface()},
                               {"menuOpen", manager.menuOpen()},
                               {"shellMenuOpen", manager.property("shellMenuOpen")},
                               {"hostActive", scene.view.isActive()}};
        };
        qInfo() << "Menu-key input state before typing" << inputState();
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api), 3000);
        const auto beforeKeys = deliveredKeys;
        QVERIFY(typeAscii(&scene.view,"ok"));
        qInfo() << "Menu-key input state after typing" << inputState()
                << "forwarded keyboard events" << deliveredKeys - beforeKeys;
        const auto recordInput = qScopeGuard([&] {
            qInfo() << "Menu-key owned client at completion" << QDBusReply<QString>(api.call("InputState")).value();
        });
        QDBusPendingReply<QString> reply=api.asyncCall("Text");
        QVERIFY(QTest::qWaitFor([&]{return reply.isFinished();},2000));
        QTRY_COMPARE(QDBusReply<QString>(api.call("Text")).value(),QString("ok"));
        api.call("Finish");
        QTRY_COMPARE(app.state(),QProcess::NotRunning);
    }
    void commandTabCyclesApplicationsByNameLikeMacOS9()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess first_,second;
        const auto stop=qScopeGuard([&]{for(auto *p:{&first_,&second})if(p->state()!=QProcess::NotRunning){p->kill();p->waitForFinished(3000);}});
        // Started in reverse name order, so stacking order and name order differ.
        for(auto [process,name]:{std::pair{&second,"Zulu"},std::pair{&first_,"Alpha"}}) {
            process->setProcessEnvironment(scene.childEnvironment());
            process->start(QCoreApplication::applicationDirPath()+"/native-probe",{name});
            QVERIFY(process->waitForStarted());
            QTRY_VERIFY(!first(manager,QString("Native Probe ")+name).isEmpty());
        }
        const auto active=[&]{return manager.snapshot().value("activeApplication").toString();};
        const auto names=[&]{QStringList out;
            QVariant result;
            QMetaObject::invokeMethod(scene.view.rootObject(),"applicationsByName",Q_RETURN_ARG(QVariant,result));
            for(const auto &app:result.toList())out<<app.toMap().value("name").toString();
            return out;};
        // The Application menu's list is by name.
        const auto list=names();
        QCOMPARE(list.size(),3);
        QStringList sorted=list;
        std::sort(sorted.begin(),sorted.end(),[](const QString &a,const QString &b){return a.compare(b,Qt::CaseInsensitive)<0;});
        QCOMPARE(list,sorted);
        QVERIFY(list.contains("Finder"));
        const auto tab=[&](bool back) {
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Meta,Qt::MetaModifier,125);
            if(back)nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Shift,Qt::MetaModifier|Qt::ShiftModifier,42);
            const auto key=back?Qt::Key_Backtab:Qt::Key_Tab;
            const auto modifiers=back?Qt::MetaModifier|Qt::ShiftModifier:Qt::MetaModifier;
            nativeKeyEvent(&scene.view,QEvent::KeyPress,key,modifiers,15);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,key,modifiers,15);
            if(back)nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Shift,Qt::MetaModifier,42);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Meta,Qt::NoModifier,125);
        };
        // Each press brings the next application in that list forward, wrapping.
        auto at=list.indexOf(active());
        QVERIFY(at>=0);
        for(int step=1;step<=4;++step) {
            tab(false);
            QTRY_COMPARE(active(),list.at((at+step)%3));
        }
        at=(at+4)%3;
        tab(true);
        QTRY_COMPARE(active(),list.at((at+2)%3));
        QVERIFY(capture(scene.view,"command-tab.png"));
    }
    void applicationsAskForAttentionAndRingTheBellLikeMacOS9()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        auto *attention=manager.findChild<ApplicationAttention *>();
        auto *sounds=qobject_cast<AlertSounds *>(manager.alertSounds());
        QVERIFY(attention && sounds);
        QProcess ask,busy;
        const auto stop=qScopeGuard([&]{for(auto *p:{&ask,&busy})if(p->state()!=QProcess::NotRunning){p->kill();p->waitForFinished(3000);}});
        const auto start=[&](QProcess &process,const QString &name) {
            process.setProcessEnvironment(scene.childEnvironment());
            process.start(QCoreApplication::applicationDirPath()+"/native-probe",{name});
            return process.waitForStarted() && QTest::qWaitFor([&]{return !first(manager,"Native Probe "+name).isEmpty();},10000);
        };
        const auto item=[&](const QString &name){return visualItem(scene.view.contentItem(),
            "client-"+QString::number(first(manager,"Native Probe "+name).value("id").toULongLong()));};
        QVERIFY(start(ask,"Ask"));
        QDBusInterface askApi("org.frankenstein.NativeProbeAsk","/Probe","org.frankenstein.NativeProbe");
        // Real input first, so the application has a seat to ask with.
        QTRY_VERIFY(item("Ask") && item("Ask")->hasActiveFocus());
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(askApi),3000);
        QVERIFY(typeAscii(&scene.view,"a"));
        QVERIFY(start(busy,"Busy"));
        QTRY_VERIFY(item("Busy")->hasActiveFocus());
        const auto app=[&](const QString &suffix) {
            for(const auto &value:manager.applications()) if(value.toMap().value("name").toString().endsWith(suffix))return value.toMap();
            return QVariantMap{};
        };
        const auto askName=app("Ask").value("name").toString();
        QVERIFY(!askName.isEmpty());
        // Without the keyboard, asking only marks the application: Busy keeps typing.
        QSignalSpy requested(attention,&ApplicationAttention::attentionRequested);
        askApi.call("RequestActivate");
        QTRY_COMPARE(requested.count(),1);
        QTRY_VERIFY(app("Ask").value("attention").toBool());
        QVERIFY(item("Busy")->hasActiveFocus());
        QVERIFY(app("Busy").value("active").toBool());
        // The Application menu title alternates with the waiting application's icon, and the menu marks it.
        auto *label=visualItem(scene.view.contentItem(),"application-menu");
        QVERIFY(label);
        QTRY_VERIFY_WITH_TIMEOUT(label->property("alternate").toBool(),3000);
        QTRY_VERIFY_WITH_TIMEOUT(!label->property("alternate").toBool(),3000);
        QMetaObject::invokeMethod(scene.view.rootObject(),"showMenu",Q_ARG(QVariant,QVariant(scene.view.rootObject()->property("barEntries").toList().size())));
        QTRY_VERIFY(visualItem(scene.view.contentItem(),"attention-mark-"+askName) &&
                    visualItem(scene.view.contentItem(),"attention-mark-"+askName)->isVisible());
        QVERIFY(capture(scene.view,"attention-application-menu.png"));
        QMetaObject::invokeMethod(scene.view.rootObject(),"closeMenu",Q_ARG(QVariant,true));
        // Coming forward clears it.
        manager.activateApplication(app("Ask").value("id").toULongLong());
        QTRY_VERIFY(app("Ask").value("active").toBool());
        QTRY_VERIFY(!app("Ask").value("attention").toBool());
        // A token the desktop granted brings an application forward, once.
        manager.activateApplication(app("Busy").value("id").toULongLong());
        QTRY_VERIFY(item("Busy")->hasActiveFocus());
        const auto token=attention->grantedToken();
        askApi.call("RequestActivateWithToken",token);
        QTRY_VERIFY(app("Ask").value("active").toBool());
        QTRY_VERIFY(item("Ask")->hasActiveFocus());
        manager.activateApplication(app("Busy").value("id").toULongLong());
        QTRY_VERIFY(item("Busy")->hasActiveFocus());
        askApi.call("RequestActivateWithToken",token);
        QTRY_VERIFY(app("Ask").value("attention").toBool());
        QVERIFY(app("Busy").value("active").toBool());
        // The bell plays the alert sound; silent here, so the menu bar flashes.
        QSignalSpy flashed(sounds,&AlertSounds::flash);
        QSignalSpy played(sounds,&AlertSounds::played);
        QDBusInterface busyApi("org.frankenstein.NativeProbeBusy","/Probe","org.frankenstein.NativeProbe");
        busyApi.call("Beep");
        QTRY_COMPARE(flashed.count()+played.count(),1);
        askApi.call("Finish");busyApi.call("Finish");
        QTRY_COMPARE(ask.state(),QProcess::NotRunning);
        QTRY_COMPARE(busy.state(),QProcess::NotRunning);
    }
    void middleClickPastesThePrimarySelectionAcrossApplications()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        QProcess first_,second,x11;
        const auto stop=qScopeGuard([&]{for(auto *p:{&first_,&second,&x11})if(p->state()!=QProcess::NotRunning){p->kill();p->waitForFinished(3000);}});
        const auto start=[&](QProcess &process,const QString &name,bool viaX11) {
            auto environment=scene.childEnvironment();
            if(viaX11) {
                environment.insert("QT_QPA_PLATFORM","xcb");
                environment.insert("QT_PLUGIN_PATH",QCoreApplication::applicationDirPath()+"/plugins");
                environment.insert("QT_STYLE_OVERRIDE","Frankenstein");
            }
            process.setProcessEnvironment(environment);
            process.start(QCoreApplication::applicationDirPath()+"/native-probe",{name});
            return process.waitForStarted() && QTest::qWaitFor([&]{return !first(manager,"Native Probe "+name).isEmpty();},10000);
        };
        QVERIFY(start(first_,"PrimaryA",false));
        QVERIFY(start(second,"PrimaryB",false));
        QVERIFY(start(x11,"PrimaryX",true));
        const auto api=[](const QString &name){return new QDBusInterface("org.frankenstein.NativeProbe"+name,"/Probe","org.frankenstein.NativeProbe");};
        std::unique_ptr<QDBusInterface> a(api("PrimaryA")),b(api("PrimaryB")),x(api("PrimaryX"));
        QVERIFY(a->isValid() && b->isValid() && x->isValid());
        const auto apiFor=[&](const QString &name)->QDBusInterface &{return name=="PrimaryA"?*a:name=="PrimaryB"?*b:*x;};
        const auto focus=[&](const QString &name) {
            const auto id=first(manager,"Native Probe "+name).value("id").toULongLong();
            manager.activate(id);
            auto *item=qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
            // The client's own window activation must arrive too, before input is sent to it.
            return item && QTest::qWaitFor([&]{return item->hasActiveFocus();},3000) &&
                   QTest::qWaitFor([&]{return probeAcceptsTextInput(apiFor(name));},3000) ? item : nullptr;
        };
        const auto middleClick=[&](QWaylandQuickItem *item) {
            const auto point=item->mapToScene(item->mapFromSurface(QPointF(120,120))).toPoint();
            QTest::mouseMove(&scene.view,point);
            QTest::qWait(30);
            QTest::mouseClick(&scene.view,Qt::MiddleButton,{},point);
        };
        const auto text=[&](QDBusInterface &probe) {
            // Pasting needs the compositor to service the transfer: never block on it.
            QDBusPendingReply<QString> reply=probe.asyncCall("Text");
            if(!QTest::qWaitFor([&]{return reply.isFinished();},2000) || reply.isError())
                return QString("<inspection unavailable>");
            return reply.value();
        };
        // Wayland to Wayland.
        QVERIFY(focus("PrimaryA"));
        a->call("SetSelection",QString::fromUtf8("primary ✓ wayland"));
        QTest::qWait(150);
        auto *itemB=focus("PrimaryB");
        QVERIFY(itemB);
        middleClick(itemB);
        QTRY_VERIFY2(text(*b).contains(QString::fromUtf8("primary ✓ wayland")),qPrintable(text(*b)));
        // Wayland to X11, through the bridge.
        auto *itemX=focus("PrimaryX");
        QVERIFY(itemX);
        middleClick(itemX);
        QTRY_VERIFY2(text(*x).contains(QString::fromUtf8("primary ✓ wayland")),qPrintable(text(*x)));
        // X11 to Wayland.
        x->call("SetSelection",QString::fromUtf8("primary ✓ x11"));
        QTest::qWait(300);
        auto *itemA=focus("PrimaryA");
        QVERIFY(itemA);
        middleClick(itemA);
        QTRY_VERIFY2(text(*a).contains(QString::fromUtf8("primary ✓ x11")),qPrintable(text(*a)));
        for(auto *probe:{a.get(),b.get(),x.get()})probe->call("Finish");
        QTRY_COMPARE(first_.state(),QProcess::NotRunning);
        QTRY_COMPARE(second.state(),QProcess::NotRunning);
        QTRY_COMPARE(x11.state(),QProcess::NotRunning);
    }
    void typingOnTheDesktopSelectsAnIconByName()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        auto *items=qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        for(const auto *name:{"Mail","Mailbox Notes","QuickTime Player"}) {
            QFile file(items->directory()+"/"+name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        items->refresh();
        QVERIFY(manager.activateDesktop());
        QTRY_VERIFY(manager.desktopActive());
        QTRY_VERIFY(scene.view.rootObject()->hasActiveFocus());
        items->clearSelection();
        // Typed together, letters build one name.
        QVERIFY(typeAscii(&scene.view,"mailb"));
        QTRY_COMPARE(items->selectedNames(),QString("Mailbox Notes"));
        // After a pause, typing starts a new name; nothing starts with n, so the next name wins.
        QTest::qWait(1200);
        QVERIFY(typeAscii(&scene.view,"n"));
        QTRY_COMPARE(items->selectedNames(),QString("QuickTime Player"));
        // Command keys are not typing.
        QTest::qWait(1200);
        commandKey(&scene.view,Qt::Key_M,50);
        QTest::qWait(100);
        QCOMPARE(items->selectedNames(),QString("QuickTime Player"));
    }
    void retainedClipboardOffersKeepTheirOriginalData()
    {
        // An application keeps its offer for clipboard A, the clipboard becomes B, and it then
        // pastes from the offer for A. It must get A, never B. The reader is a real window: the
        // desktop takes keyboard focus back from a surface that is not one.
        QGuiApplication::clipboard()->clear(); // No earlier test's shell copy.
        NativeScene scene;
        QVERIFY(scene.show());
        auto *clipboard = qobject_cast<Clipboard *>(scene.manager->clipboard());
        QVERIFY(clipboard && clipboard->active());
        OwnedProcess paste;
        QVERIFY2(startPasteProbe(paste, scene), qPrintable(paste.lines.join(" | ")));
        QTest::qWait(300); // Any clipboard offered with the keyboard has arrived.
        bool offered = false;
        const int previous = paste.latest("selection ", 0).mid(10).toInt(&offered);
        clipboard->copyText("original offer A");
        const int alpha = pasteOffer(paste, offered ? previous : -1);
        QVERIFY2(alpha >= 0, qPrintable(paste.lines.join(" | ")));
        clipboard->copyText("new offer B");
        const int bravo = pasteOffer(paste, alpha);
        QVERIFY2(bravo >= 0, qPrintable(paste.lines.join(" | ")));
        QVERIFY(paste.send(QString("read %1 text/plain;charset=utf-8").arg(alpha)));
        QCOMPARE(paste.next("data"), QString("data %1 16 original_offer_A 0").arg(alpha));
        QVERIFY(paste.send(QString("read %1 text/plain;charset=utf-8").arg(bravo)));
        QCOMPARE(paste.next("data"), QString("data %1 11 new_offer_B 0").arg(bravo));
    }
    void aStalledPasteNeverBlocksTheDesktop()
    {
        // A paste from the compositor-owned clipboard whose reader stops reading, then goes away
        // mid-transfer. Its pipe fills; the compositor must go on running and delivering input.
        QGuiApplication::clipboard()->clear(); // No earlier test's shell copy.
        NativeScene scene;
        QVERIFY(scene.show());
        retainLargeClipboard(scene);
        if (QTest::currentTestFailed())
            return;
        OwnedProcess paste;
        QVERIFY2(startPasteProbe(paste, scene), qPrintable(paste.lines.join(" | ")));
        const int offer = pasteOffer(paste);
        QVERIFY2(offer >= 0, qPrintable(paste.lines.join(" | ")));
        {
            Heartbeat heartbeat(paste.input());
            QVERIFY(paste.send(QString("stall %1 text/plain").arg(offer)));
            QCOMPARE(paste.next("stalling"), QString("stalling 1"));
            QVERIFY(paste.send("sync"));
            QVERIFY(!paste.next("synced", 10000).isEmpty());
            // While the reader is still stalled: input reaches the application, and the
            // compositor's thread keeps running.
            QTest::keyClick(&scene.view, Qt::Key_A);
            QVERIFY(!paste.next("key 30 ").isEmpty());
            QTest::qWait(1500);
            QVERIFY2(heartbeat.releases() == 0 && heartbeat.worst() < 1000,
                     qPrintable(QString("The compositor stopped for %1 ms").arg(heartbeat.worst())));
        }
        QVERIFY(paste.send("drop"));
        QCOMPARE(paste.next("dropped"), QString("dropped 1"));
        // Still serving the whole clipboard, from the same offer.
        QVERIFY(paste.send(QString("read %1 text/plain").arg(offer)));
        QCOMPARE(paste.next("data", 20000), QString("data %1 4194304 pppppppppppppppp 1").arg(offer));
    }
    void aPasteClosedAtOnceNeverKillsTheDesktop()
    {
        // Readers that close their end before anything is written, as a crashed application's
        // does. The compositor's writes then fail with EPIPE, and SIGPIPE, which the desktop does
        // not ignore, would kill it.
        QGuiApplication::clipboard()->clear(); // No earlier test's shell copy.
        NativeScene scene;
        QVERIFY(scene.show());
        retainLargeClipboard(scene);
        if (QTest::currentTestFailed())
            return;
        OwnedProcess paste;
        QVERIFY2(startPasteProbe(paste, scene), qPrintable(paste.lines.join(" | ")));
        const int offer = pasteOffer(paste);
        QVERIFY2(offer >= 0, qPrintable(paste.lines.join(" | ")));
        for (int i = 0; i < 20; ++i)
        {
            QVERIFY(paste.send(QString("close %1 text/plain").arg(offer)));
            QCOMPARE(paste.next("closed"), QString("closed %1").arg(offer));
        }
        QTest::qWait(500);
        QVERIFY(paste.send(QString("read %1 text/plain").arg(offer)));
        QCOMPARE(paste.next("data", 20000), QString("data %1 4194304 pppppppppppppppp 1").arg(offer));
    }
    void manyStalledPastesUseBoundedResources()
    {
        // 64 pastes stalled at once. The compositor keeps running, its writers stay few, and all
        // of them let go when the readers go away.
        QGuiApplication::clipboard()->clear(); // No earlier test's shell copy.
        NativeScene scene;
        QVERIFY(scene.show());
        retainLargeClipboard(scene);
        if (QTest::currentTestFailed())
            return;
        OwnedProcess paste;
        QVERIFY2(startPasteProbe(paste, scene), qPrintable(paste.lines.join(" | ")));
        const int offer = pasteOffer(paste);
        QVERIFY2(offer >= 0, qPrintable(paste.lines.join(" | ")));
        const int before = threadCount();
        constexpr int stalled = 64;
        {
            Heartbeat heartbeat(paste.input());
            QString commands;
            for (int i = 0; i < stalled; ++i)
                commands += QString("stall %1 text/plain\n").arg(offer);
            QVERIFY(paste.send(commands + "sync"));
            QVERIFY2(!paste.next(QString("stalling %1").arg(stalled)).isEmpty(), qPrintable(paste.lines.join(" | ")));
            QVERIFY(!paste.next("synced", 20000).isEmpty());
            QTest::keyClick(&scene.view, Qt::Key_A);
            QVERIFY(!paste.next("key 30 ").isEmpty());
            QTest::qWait(1000);
            QVERIFY2(heartbeat.releases() == 0 && heartbeat.worst() < 1000,
                     qPrintable(QString("The compositor stopped for %1 ms").arg(heartbeat.worst())));
            // A bounded number of writers (with room for the desktop's own threads), however many
            // readers stall.
            const int during = threadCount();
            QVERIFY2(during - before <= 12,
                     qPrintable(QString("%1 threads for %2 stalled pastes, %3 before").arg(during).arg(stalled).arg(before)));
        }
        QVERIFY(paste.send("drop"));
        QCOMPARE(paste.next("dropped"), QString("dropped %1").arg(stalled));
        QTRY_VERIFY_WITH_TIMEOUT(threadCount() - before <= 2, 5000);
        QVERIFY(paste.send(QString("read %1 text/plain").arg(offer)));
        QCOMPARE(paste.next("data", 20000), QString("data %1 4194304 pppppppppppppppp 1").arg(offer));
    }
    void commandLineClipboardToolsReadAndWriteTheClipboard()
    {
        // wl-paste / wl-copy (wl-clipboard), as terminal programs use them: no window of their own.
        if(!QFileInfo::exists("/usr/bin/wl-paste")) QSKIP("wl-clipboard is not installed.");
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        QProcess app;
        app.setProcessEnvironment(scene.childEnvironment());
        app.start(QCoreApplication::applicationDirPath()+"/native-probe",{"ClipCli"});
        const auto stop=qScopeGuard([&]{if(app.state()!=QProcess::NotRunning){app.kill();app.waitForFinished(3000);}});
        QTRY_VERIFY(!first(manager,"Native Probe ClipCli").isEmpty());
        QDBusInterface api("org.frankenstein.NativeProbeClipCli","/Probe","org.frankenstein.NativeProbe");
        QTRY_VERIFY_WITH_TIMEOUT(probeAcceptsTextInput(api),3000);
        api.call("SetClipboard",QString::fromUtf8("from an application ✓"));
        QTest::qWait(300);
        const auto windowsBefore=manager.snapshot().value("windows").toList().size();
        const auto run=[&](const QString &program,const QStringList &arguments,const QByteArray &input,QByteArray *output) {
            QProcess tool;
            tool.setProcessEnvironment(scene.childEnvironment());
            tool.start(program,arguments);
            if(!tool.waitForStarted())return -2;
            if(!input.isEmpty()){tool.write(input);tool.closeWriteChannel();}
            // The compositor lives in this process: keep its loop running while the tool works.
            // wl-copy forks to serve its copy; that child must finish too once its copy is taken.
            if(!QTest::qWaitFor([&]{return tool.state()==QProcess::NotRunning;},5000)){tool.kill();tool.waitForFinished(2000);return -1;}
            if(output)*output=tool.readAllStandardOutput();
            qInfo().noquote()<<program<<"stderr:"<<tool.readAllStandardError().left(300);
            return tool.exitCode();
        };
        QByteArray pasted;
        const int pasteExit=run("/usr/bin/wl-paste",{"--no-newline"},{},&pasted);
        qInfo()<<"wl-paste exit"<<pasteExit<<"output"<<pasted;
        QCOMPARE(pasteExit,0);
        QCOMPARE(QString::fromUtf8(pasted),QString::fromUtf8("from an application ✓"));
        // No window flashes up for the tool.
        QCOMPARE(manager.snapshot().value("windows").toList().size(),windowsBefore);
        // wl-copy, then an application pastes it. The application keeps the keyboard (wl-copy made
        // a window of its own and kept running before), and wl-copy finishes (run() waits for it).
        QCOMPARE(run("/usr/bin/wl-copy",{},QByteArray("from the command line ✓"),nullptr),0);
        QCOMPARE(manager.snapshot().value("windows").toList().size(),windowsBefore);
        QVERIFY(probeAcceptsTextInput(api));
        // Its background copy server exits once the desktop has taken the copy.
        const auto socket=QByteArray("WAYLAND_DISPLAY=")+scene.compositor.socketName();
        const auto servingCopy=[&] {
            for(const auto &pid:QDir("/proc").entryList(QDir::Dirs)) {
                if(!pid.toInt())continue;
                if(!bytes("/proc/"+pid+"/cmdline").startsWith("/usr/bin/wl-copy"))continue;
                if(bytes("/proc/"+pid+"/environ").split('\0').contains(socket))return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(!servingCopy(),5000);
        commandKey(&scene.view,Qt::Key_V,47);
        const auto text=[&]{QDBusPendingReply<QString> reply=api.asyncCall("Text");
            return QTest::qWaitFor([&]{return reply.isFinished();},2000)&&!reply.isError()?reply.value():QString();};
        QTRY_COMPARE_WITH_TIMEOUT(text(),QString::fromUtf8("from the command line ✓"),5000);
        api.call("Finish");
        QTRY_COMPARE(app.state(),QProcess::NotRunning);
    }
    void titleBarBoxesPressInAndCancelWhenDraggedOff()
    {
        // Mac OS 9.2.2 tracking (psn_tch, 2026-10-22): a box is drawn pressed in only while held over
        // it, and acts only when released over it.
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup = qScopeGuard([&] { if (probe.state() != QProcess::NotRunning) { probe.kill(); probe.waitForFinished(3000); } });
        probe.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Boxes"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY(!first(*scene.manager, "Native Probe Boxes").isEmpty());
        const auto id = first(*scene.manager, "Native Probe Boxes").value("id").toULongLong();
        QQuickItem *box = nullptr;
        QTRY_VERIFY((box = visualItem(scene.view.contentItem(), "shade-box-" + QString::number(id))) && box->isVisible());
        // A first click on an inactive window only activates it, and a new window is placed after it
        // maps: press once it is active and has stopped moving (the sanitizer build is slow to settle).
        QTRY_VERIFY(first(*scene.manager, "Native Probe Boxes").value("active").toBool());
        QPoint centre;
        QTRY_VERIFY([&] {
            const auto now = box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint();
            const bool settled = now == centre;
            centre = now;
            return settled;
        }());
        QTest::qWait(200);
        centre = box->mapToScene(QPointF(box->width() / 2, box->height() / 2)).toPoint();
        QTest::mousePress(&scene.view, Qt::LeftButton, {}, centre);
        QTRY_VERIFY(box->property("down").toBool());
        QVERIFY(capture(scene.view, "title-box-pressed.png"));
        QTest::mouseMove(&scene.view, centre + QPoint(0, 60));
        QTRY_VERIFY(!box->property("down").toBool());
        QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, centre + QPoint(0, 60));
        QTest::qWait(300);
        QVERIFY(!first(*scene.manager, "Native Probe Boxes").value("collapsed").toBool());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, centre);
        QTRY_VERIFY(first(*scene.manager, "Native Probe Boxes").value("collapsed").toBool());
        QDBusInterface("org.frankenstein.NativeProbeBoxes", "/Probe", "org.frankenstein.NativeProbe").call("Finish");
    }
    void tripleClickingTheTitleBarFillsTheScreenAndBack()
    {
        // psn_tch, 2026-10-23: two clicks on the title bar are WindowShade (at once, as before); a
        // third fills the screen below the menu bar; three again put the window back.
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup = qScopeGuard([&] { if (probe.state() != QProcess::NotRunning) { probe.kill(); probe.waitForFinished(3000); } });
        probe.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Triple"});
        QVERIFY(probe.waitForStarted());
        const auto window = [&] { return first(*scene.manager, "Native Probe Triple"); };
        QTRY_VERIFY(!window().isEmpty());
        const auto id = window().value("id").toULongLong();
        QTRY_VERIFY(window().value("active").toBool());
        QQuickItem *bar = nullptr;
        QTRY_VERIFY((bar = visualItem(scene.view.contentItem(), "title-bar-" + QString::number(id))));
        // Settled where it was placed, so each click lands on the same title bar.
        QVariantMap before;
        QTRY_VERIFY([&] { const auto now = window(); const bool same = now == before; before = now; return same; }());
        QTest::qWait(200);
        before = window();
        const auto click = [&] { return bar->mapToScene(QPointF(60, bar->height() / 2)).toPoint(); };
        QTest::mouseDClick(&scene.view, Qt::LeftButton, {}, click());
        QTRY_VERIFY(window().value("collapsed").toBool());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, click());
        const auto screen = scene.manager->primaryDisplayRect();
        QTRY_VERIFY_WITH_TIMEOUT(!window().value("collapsed").toBool() &&
                                 window().value("width").toInt() == screen.width() - 6, 5000);
        QTRY_COMPARE(window().value("x").toInt(), screen.x());
        QTRY_COMPARE(window().value("y").toInt(), screen.y() + 26);
        QTRY_COMPARE(window().value("height").toInt(), screen.height() - 26 - 27);
        QVERIFY(capture(scene.view, "title-bar-triple-click-filled.png"));
        // Three again: back where and how big it was.
        QTRY_VERIFY(!window().value("shadeAnimating").toBool());
        QTest::mouseDClick(&scene.view, Qt::LeftButton, {}, click());
        QTRY_VERIFY(window().value("collapsed").toBool());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, click());
        QTRY_VERIFY_WITH_TIMEOUT(!window().value("collapsed").toBool() &&
                                 window().value("width").toInt() == before.value("width").toInt(), 5000);
        QTRY_COMPARE(window().value("height").toInt(), before.value("height").toInt());
        QTRY_COMPARE(window().value("x").toInt(), before.value("x").toInt());
        QTRY_COMPARE(window().value("y").toInt(), before.value("y").toInt());
        QDBusInterface("org.frankenstein.NativeProbeTriple", "/Probe", "org.frankenstein.NativeProbe").call("Finish");
    }
    void aClickOnAWindowClosesAnOpenMenuAndGoesNoFurther()
    {
        // psn_tch, 2026-10-23: a Control Strip menu stayed open until something was chosen when the
        // click outside it landed on a window. As in Mac OS 9, the click only closes the menu.
        NativeScene scene;
        QVERIFY(scene.show());
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup = qScopeGuard([&] { if (probe.state() != QProcess::NotRunning) { probe.kill(); probe.waitForFinished(3000); } });
        probe.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Dismiss"});
        QVERIFY(probe.waitForStarted());
        const auto window = [&] { return first(*scene.manager, "Native Probe Dismiss"); };
        QTRY_VERIFY(!window().isEmpty() && window().value("active").toBool());
        QVariantMap before;
        QTRY_VERIFY([&] { const auto now = window(); const bool same = now == before; before = now; return same; }());
        const QPoint inside(window().value("x").toInt() + 120, window().value("y").toInt() + 120);
        auto *module = visualItem(scene.view.contentItem(), "control-strip-bluetooth");
        QVERIFY(module);
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, module->mapToScene(QPointF(8, 8)).toPoint());
        auto *menu = visualItem(scene.view.contentItem(), "control-strip-menu");
        QTRY_VERIFY(menu->isVisible());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, inside);
        QTRY_VERIFY(!menu->isVisible());
        QVERIFY(!scene.manager->menuOpen());
        // The Apple menu too.
        auto *apple = visualItem(scene.view.contentItem(), "bar-entry-0");
        QVERIFY(apple);
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, apple->mapToScene(QPointF(10, 10)).toPoint());
        QTRY_VERIFY(scene.manager->menuOpen());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, inside);
        QTRY_VERIFY(!scene.manager->menuOpen());
        QDBusInterface("org.frankenstein.NativeProbeDismiss", "/Probe", "org.frankenstein.NativeProbe").call("Finish");
    }
    void fixedSizeWindowsTooBigForTheScreenAreShownFullScreen()
    {
        // psn_tch, 2026-10-22 (Daggerfall Unity's launcher): a window whose client fixed its size too
        // big to show framed below the menu bar is full screen, and stays so if the client asks to
        // leave; a fixed window that fits is framed as before.
        NativeScene scene;
        QVERIFY(scene.show());
        const auto run = [&](const QString &label, const QStringList &extra)
        {
            auto *probe = new QProcess(&scene.view);
            probe->setProcessEnvironment(scene.childEnvironment());
            probe->start(QCoreApplication::applicationDirPath() + "/native-probe", QStringList{label} + extra);
            return probe;
        };
        const auto stop = [](QProcess *probe) { if (probe->state() != QProcess::NotRunning) { probe->kill(); probe->waitForFinished(3000); } };
        auto *oversized = run("Oversized", {"--fixed=1120x760"});
        const auto cleanupOversized = qScopeGuard([&] { stop(oversized); });
        QTRY_VERIFY(first(*scene.manager, "Native Probe Oversized").value("fullScreen").toBool());
        QDBusInterface big("org.frankenstein.NativeProbeOversized", "/Probe", "org.frankenstein.NativeProbe");
        QTRY_VERIFY(big.isValid());
        QTRY_COMPARE(QDBusReply<int>(big.call("Height")).value(), 760);
        big.call("Finish");
        auto *fits = run("Fits", {"--fixed=600x400"});
        const auto cleanupFits = qScopeGuard([&] { stop(fits); });
        QTRY_VERIFY(!first(*scene.manager, "Native Probe Fits").isEmpty());
        QTest::qWait(500);
        QVERIFY(!first(*scene.manager, "Native Probe Fits").value("fullScreen").toBool());
        QVERIFY(first(*scene.manager, "Native Probe Fits").value("serverDecorated").toBool());
        QDBusInterface("org.frankenstein.NativeProbeFits", "/Probe", "org.frankenstein.NativeProbe").call("Finish");
        auto *leaving = run("Leaving", {"--fixed=1120x760", "--start-fullscreen", "--leave-fullscreen-after=800"});
        const auto cleanupLeaving = qScopeGuard([&] { stop(leaving); });
        QTRY_VERIFY(first(*scene.manager, "Native Probe Leaving").value("fullScreen").toBool());
        QTest::qWait(1500);
        QVERIFY(first(*scene.manager, "Native Probe Leaving").value("fullScreen").toBool());
        QVERIFY(capture(scene.view, "oversized-fixed-window.png"));
        QDBusInterface("org.frankenstein.NativeProbeLeaving", "/Probe", "org.frankenstein.NativeProbe").call("Finish");
    }
    void lateActivationRequestsDoNotCoverANewerWindow()
    {
        // psn_tch, 2026-10-22: Firefox's second download window opened behind everything. Closing the
        // first, Firefox asked to bring its main window back; that request arrived after the next
        // window had come forward, and covered it. A request now counts only while the keyboard
        // focus it was made under lasts.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        LineProcess probe;
        probe.process.setProcessEnvironment(scene.childEnvironment());
        probe.process.start(QCoreApplication::applicationDirPath() + "/activation-probe", {});
        QVERIFY(probe.process.waitForStarted());
        QVERIFY(!probe.next("ready").isEmpty());
        QTRY_VERIFY(!first(manager, "Activation Probe A").isEmpty() && !first(manager, "Activation Probe B").isEmpty());
        const auto a = first(manager, "Activation Probe A").value("id").toULongLong();
        const auto active = [&](const char *title) { return first(manager, title).value("active").toBool(); };
        const auto token = [&](const char *window)
        {
            probe.process.write(QByteArray("token ") + window + "\n");
            return probe.next("token").section(' ', 1);
        };
        const auto activate = [&](const char *window, const QString &value)
        {
            probe.process.write(QByteArray("activate ") + window + " " + value.toLatin1() + "\n");
            return !probe.next("sent").isEmpty();
        };
        // Asked and used while the focus is unchanged: B comes forward.
        manager.activate(a);
        QTRY_VERIFY(active("Activation Probe A"));
        QTest::qWait(200);
        const auto prompt = token("A");
        QVERIFY(prompt.size() >= 32);
        QVERIFY(activate("B", prompt));
        QTRY_VERIFY(active("Activation Probe B"));
        // Asked while B had the focus, used after A came forward: A stays in front.
        QTest::qWait(200);
        const auto late = token("B");
        manager.activate(a);
        QTRY_VERIFY(active("Activation Probe A"));
        QTest::qWait(200);
        QVERIFY(activate("B", late));
        QTest::qWait(500);
        QVERIFY(active("Activation Probe A"));
        QVERIFY(!active("Activation Probe B"));
        probe.process.write("quit\n");
        QVERIFY(probe.process.waitForFinished(3000));
    }
    void windowsSizedForAMonitorThatLeftShrinkToTheOneThatRemains()
    {
        // psn_tch, 2026-10-22: a window made big on the external monitor, unplugged, could not be
        // resized: its resize box was off the built-in display. It now shrinks to fit there.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        const auto both = DisplayLayout::create({{"HEADLESS-1", {0, 0, 1120, 760}, 1, "normal"},
                                                 {"EXTERNAL-1", {1120, 0, 1920, 1080}, 1, "normal"}},
                                                "HEADLESS-1", DisplayLayout::Mode::Extend);
        const auto laptop = DisplayLayout::create({{"HEADLESS-1", {0, 0, 1120, 760}, 1, "normal"}}, "HEADLESS-1",
                                                  DisplayLayout::Mode::Extend);
        QVERIFY(both && laptop);
        manager.setDisplayLayout(*both);
        QProcess probe;
        probe.setProcessEnvironment(scene.childEnvironment());
        const auto cleanup = qScopeGuard([&] { if (probe.state() != QProcess::NotRunning) { probe.kill(); probe.waitForFinished(3000); } });
        probe.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Big"});
        QVERIFY(probe.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native Probe Big").isEmpty(), 10000);
        const auto id = first(manager, "Native Probe Big").value("id").toULongLong();
        QDBusInterface api("org.frankenstein.NativeProbeBig", "/Probe", "org.frankenstein.NativeProbe");
        QTRY_VERIFY(api.isValid());
        manager.moveWindow(id, 1200, 60);
        manager.resizeWindow(id, 1700, 950);
        QTRY_COMPARE(first(manager, "Native Probe Big").value("width").toInt(), 1700);
        QTRY_COMPARE(first(manager, "Native Probe Big").value("x").toInt(), 1200);

        manager.setDisplayLayout(*laptop);
        const auto window = [&] { return first(manager, "Native Probe Big"); };
        QTRY_VERIFY(window().value("width").toInt() <= 1120 - 12 && window().value("height").toInt() <= 760 - 60);
        QTRY_COMPARE(QDBusReply<int>(api.call("Width")).value(), window().value("width").toInt());
        // The whole frame (the position is its corner), title bar and resize box, is on the built-in display.
        const QRect frame(window().value("x").toInt(), window().value("y").toInt(),
                          window().value("width").toInt() + 6, window().value("height").toInt() + 27);
        QVERIFY2(QRect(0, 26, 1120, 760 - 26).contains(frame),
                 qPrintable(QString("frame %1,%2 %3x%4").arg(frame.x()).arg(frame.y()).arg(frame.width()).arg(frame.height())));
        QVERIFY(capture(scene.view, "unplugged-monitor-window.png"));
        api.call("Finish");
    }
    void wineConfigurationUsesOnlyADisposablePrefix()
    {
        if (X11Bridge::helperPath().isEmpty()) QSKIP("This build explicitly disables X11 support.");
        if (!QFileInfo::exists("/usr/bin/winecfg")) QSKIP("Wine is not installed.");
        NativeScene scene;
        QVERIFY(scene.show());
        QTRY_VERIFY_WITH_TIMEOUT(scene.x11.ready(),10000);
        auto environment=scene.childEnvironment();
        environment.insert("WINEPREFIX",scene.files.path()+"/wine-prefix");
        environment.insert("WINEARCH","win64");
        environment.insert("WINEDEBUG","-all");
        // Force the path under test, and suppress optional Wine Mono/Gecko
        // installers and desktop-association helpers. No user prefix or game.
        environment.insert("WINEDLLOVERRIDES","winewayland.drv=d;mscoree,mshtml,winemenubuilder.exe=");
        environment.insert("HOME",scene.files.path()+"/wine-home");
        QVERIFY(QDir().mkpath(environment.value("HOME")));
        QProcess wine;
        const auto cleanup=qScopeGuard([&]{
            QProcess server;
            server.setProcessEnvironment(environment); // -k addresses ONLY this prefix.
            server.start("/usr/bin/wineserver",{"-k"});
            server.waitForFinished(3000);
            if(wine.state()!=QProcess::NotRunning){wine.kill();wine.waitForFinished(3000);}
        });
        wine.setProcessEnvironment(environment);
        wine.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/winecfg-stderr.txt");
        wine.setStandardOutputFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/winecfg-stdout.txt");
        wine.start("/usr/bin/winecfg");
        QVERIFY(wine.waitForStarted());
        // A new prefix is bootstrapped (wineboot) before winecfg maps: 20.9 s in the 2026-10-22
        // daily-batch gate. This waits on Wine, not on the desktop.
        QTRY_VERIFY_WITH_TIMEOUT(!first(*scene.manager,"Wine configuration").isEmpty(),40000);
        const auto window=first(*scene.manager,"Wine configuration");
        QVERIFY(window.value("serverDecorated").toBool());
        const auto id=window.value("id").toULongLong();
        scene.manager->moveWindow(id,170,90);
        scene.manager->activate(id);
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-x11-winecfg.png"));
        // Close normally, not by killing Wine: the WM_DELETE path must work.
        scene.manager->closeWindow(id);
        QTRY_VERIFY_WITH_TIMEOUT(first(*scene.manager,"Wine configuration").isEmpty(),5000);
        QTRY_COMPARE_WITH_TIMEOUT(wine.state(),QProcess::NotRunning,5000);
        QCOMPARE(wine.exitCode(),0);
        // The registry is flushed lazily by wineserver; prefix creation and the
        // real UI/normal-close checks above do not depend on that timer.
        QVERIFY(QFileInfo(environment.value("WINEPREFIX")+"/drive_c/windows").isDir());
    }
    void ymirFitsTheDesktopWithADisposableProfile()
    {
        if (!QFileInfo::exists("/usr/bin/ymir-sdl3")) QSKIP("Ymir is not installed.");
        NativeScene scene;
        QVERIFY(scene.show());
        // Disposable profile in Ymir v0.3.3's current schema: without
        // ConfigVersion the file is read as an old schema and these choices do not
        // apply. No IPL/BIOS, disc, save or user profile is read or written.
        const auto profile=scene.files.path()+"/ymir";
        QVERIFY(QDir().mkpath(profile+"/state/updates"));
        QFile settings(profile+"/Ymir.toml");
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("ConfigVersion = 5\n\n[GUI]\nOverrideUIScale = true\nUIScale = 1.0\n"
                       "RememberWindowGeometry = false\n\n[General]\nCheckForUpdates = false\n");
        settings.close();
        QFile onboarded(profile+"/state/updates/.onboarded");
        QVERIFY(onboarded.open(QIODevice::WriteOnly));
        onboarded.close();
        QProcess emulator;
        auto environment=scene.childEnvironment();
        configureKnownEmulatorEnvironment("/usr/bin/ymir-sdl3",environment); // As the desktop launches it.
        QCOMPARE(environment.value("SDL_VIDEODRIVER"),QString("wayland"));
        if (qEnvironmentVariableIsSet("FRANKENSTEIN_YMIR_RENDERER")) // Recorded comparisons only.
            environment.insert("SDL_RENDER_DRIVER",qEnvironmentVariable("FRANKENSTEIN_YMIR_RENDERER"));
        environment.insert("SDL_AUDIO_DRIVER","dummy"); // Fixture only: never the user's audio.
        environment.insert("SDL_LOGGING","render=verbose"); // Records the renderer SDL created.
        emulator.setProcessEnvironment(environment);
        emulator.setStandardOutputFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/ymir-stdout.txt");
        emulator.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/ymir-stderr.txt");
        const auto cleanup=qScopeGuard([&]{
            if(emulator.state()!=QProcess::NotRunning){emulator.kill();emulator.waitForFinished(3000);}
        });
        const auto failureCapture=qScopeGuard([&]{
            if(QTest::currentTestFailed()) capture(scene.view,"native-ymir-failure.png");
        });
        emulator.start("/usr/bin/ymir-sdl3",{"--profile",profile,"--paused"});
        QVERIFY(emulator.waitForStarted());
        qulonglong id=0;
        QVERIFY2(QTest::qWaitFor([&]{
            for(const auto &value:scene.manager->snapshot().value("windows").toList()) {
                const auto item=value.toMap();
                if(item.value("title").toString().startsWith("Ymir")) {id=item.value("id").toULongLong();return true;}
            }
            return false;
        },8000),"Ymir did not present a native window.");
        auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client && client->surface());
        auto *xdg=qobject_cast<QWaylandXdgSurface *>(client->shellSurface());
        QVERIFY(xdg);
        auto *surface=client->surface();
        QTest::qWait(600); // Observation window for evidence only; nothing below retries.
        qInfo()<<"Ymir window geometry"<<xdg->windowGeometry()<<"buffer"<<surface->bufferSize()
               <<"buffer scale"<<surface->bufferScale()<<"destination"<<surface->destinationSize()
               <<"desktop"<<scene.view.size()<<"ratio"<<scene.view.devicePixelRatio();
        // Ymir chose its own size for the desktop it was told about: the
        // compositor did not have to shrink it, so its centred Welcome dialog is
        // laid out inside the window it presents.
        const auto chosen=xdg->windowGeometry().size();
        QVERIFY2(chosen.width()<=scene.view.width()-12 && chosen.height()<=scene.view.height()-60,
                 "Ymir sized itself beyond the desktop it was told about.");
        QCOMPARE(surface->destinationSize(),chosen);
        QVERIFY(capture(scene.view,"native-ymir-interface.png"));
        QWaylandSurfaceGrabber grabber(surface);
        const auto grab=[&] {
            QImage raw;
            const auto ok=QObject::connect(&grabber,&QWaylandSurfaceGrabber::success,&grabber,[&](const QImage &image){raw=image;});
            grabber.grab();
            if (!QTest::qWaitFor([&]{return !raw.isNull();},2000)) qWarning("Ymir buffer grab timed out.");
            QObject::disconnect(ok);
            return raw.convertToFormat(QImage::Format_RGB32);
        };
        // Ymir's dialog buttons: the rightmost button of the lowest button row of
        // the Welcome dialog is OK. Locate it in the client's own buffer.
        const QColor button(0x23,0x36,0x70);
        const auto isButton=[&](QRgb pixel) {
            return qAbs(qRed(pixel)-button.red())<6 && qAbs(qGreen(pixel)-button.green())<6 &&
                   qAbs(qBlue(pixel)-button.blue())<6;
        };
        const auto lowestButton=[&](const QImage &image) -> QRect {
            for (int y=image.height()-1; y>=0; --y) {
                int run=0, end=-1;
                for (int x=0; x<image.width(); ++x) {
                    run=isButton(image.pixel(x,y)) ? run+1 : 0;
                    if (run>=24) end=x;
                }
                if (end<0) continue;
                int left=end;
                while (left>0 && isButton(image.pixel(left-1,y))) --left;
                // Measure height beside the label, not through its glyphs.
                int top=y;
                const int edge=qMin(left+3,end);
                while (top>0 && isButton(image.pixel(edge,top-1))) --top;
                return QRect(QPoint(left,top),QPoint(end,y));
            }
            return {};
        };
        const auto welcome=grab();
        QVERIFY(welcome.save(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/ymir-raw-buffer.png"));
        const auto ok=lowestButton(welcome);
        QVERIFY2(ok.isValid() && ok.width()>=40 && ok.height()>=12,"The Welcome dialog's OK button was not found.");
        const auto unitsPerPixel=double(surface->destinationSize().width())/welcome.width();
        const auto target=client->mapToScene(client->mapFromSurface(QPointF(ok.center())*unitsPerPixel)).toPoint();
        scene.manager->activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        QTest::mouseMove(&scene.view,target);
        QTest::qWait(80);
        QTest::mouseClick(&scene.view,Qt::LeftButton,{},target);
        QVERIFY2(QTest::qWaitFor([&]{return !lowestButton(grab()).isValid();},4000),
                 "Clicking OK did not dismiss Ymir's Welcome dialog.");
        QVERIFY(capture(scene.view,"native-ymir-dismissed.png"));
        // Ymir's own Alt+Return asks the desktop for full screen and back,
        // typed at a person's pace: Ymir toggles while its window reconfigures.
        const auto altReturn=[&] {
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Alt,Qt::AltModifier,56);
            QTest::qWait(40);
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Return,Qt::AltModifier,28);
            QTest::qWait(60);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Return,Qt::AltModifier,28);
            QTest::qWait(40);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier,56);
        };
        altReturn();
        QTRY_VERIFY_WITH_TIMEOUT(xdg->toplevel()->fullscreen(),4000);
        QTRY_COMPARE(xdg->windowGeometry().size(),scene.view.size());
        QTRY_COMPARE(client->mapToScene(QPointF(0,0)),QPointF(0,0));
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-ymir-fullscreen.png"));
        altReturn();
        QTRY_VERIFY_WITH_TIMEOUT(!xdg->toplevel()->fullscreen(),4000);
        QTRY_COMPARE(xdg->windowGeometry().size(),chosen);
        // Ymir's own F10 hotkey opens its Settings window (keyboard path).
        const auto before=grab();
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_F10,{},68);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_F10,{},68);
        const auto changed=[&] {
            const auto now=grab();
            if (now.size()!=before.size()) return true;
            int different=0;
            for (int y=0; y<now.height(); y+=4)
                for (int x=0; x<now.width(); x+=4)
                    different+=now.pixel(x,y)!=before.pixel(x,y);
            return different>(now.width()/4)*(now.height()/4)/20;
        };
        QVERIFY2(QTest::qWaitFor(changed,4000),"F10 did not change Ymir's interface.");
        QTest::qWait(300);
        QVERIFY(capture(scene.view,"native-ymir-settings.png"));
        QVERIFY(grab().save(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/ymir-settings-raw.png"));
        scene.manager->closeWindow(id);
        QVERIFY(QTest::qWaitFor([&]{return emulator.state()==QProcess::NotRunning;},5000));
        QCOMPARE(emulator.exitCode(),0);
        const auto log=bytes(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/ymir-stderr.txt");
        const auto created=log.mid(log.lastIndexOf("Created renderer:")).split('\n').first();
        qInfo().noquote()<<"Ymir"<<QString::fromUtf8(created);
        QVERIFY2(created.startsWith("Created renderer:"),"SDL did not report Ymir's renderer.");
        // Ymir records the windows it created; Settings proves F10 reached it.
        const auto layout=bytes(profile+"/state/imgui.ini");
        qInfo().noquote()<<"Ymir window records:"<<QString::fromUtf8(layout).split('\n').filter("[Window]").join(" ");
        QVERIFY2(layout.contains("[Window][Settings"),"Ymir did not open its Settings window.");
        // The disposable choices survived Ymir's own read and save.
        const auto saved=bytes(profile+"/Ymir.toml");
        QVERIFY(saved.contains("ConfigVersion = 5"));
        QVERIFY(saved.contains("UIScale = 1.0"));
        QVERIFY(saved.contains("CheckForUpdates = false"));
    }
    void browserCanDownloadAnImageAndOpenSaveAs()
    {
        if (!QFileInfo::exists("/usr/lib/firefox/firefox"))
            QSKIP("This browser workflow needs the installed Firefox.");
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager=*scene.manager;
        const auto profile=scene.files.path()+"/profile";
        const auto downloads=scene.files.path()+"/Downloads";
        QVERIFY(QDir().mkpath(downloads));
        QVERIFY(QDir().mkpath(profile+"/browsers/firefox"));
        QFile prefs(profile+"/browsers/firefox/prefs.js");
        QVERIFY(prefs.open(QIODevice::WriteOnly));
        prefs.write("user_pref(\"browser.aboutwelcome.enabled\",false);\n"
                    "user_pref(\"browser.shell.checkDefaultBrowser\",false);\n"
                    "user_pref(\"browser.startup.homepage_override.mstone\",\"ignore\");\n"
                    "user_pref(\"extensions.enabledScopes\",1);\n"
                    "user_pref(\"extensions.autoDisableScopes\",15);\n"
                    "user_pref(\"datareporting.policy.firstRunURL\",\"\");\n"
                    "user_pref(\"browser.download.folderList\",2);\n"
                    "user_pref(\"browser.download.useDownloadDir\",true);\n");
        prefs.write("user_pref(\"browser.download.dir\",\""+downloads.toUtf8()+"\");\n");
        prefs.write("user_pref(\"browser.download.lastDir\",\""+downloads.toUtf8()+"\");\n");
        prefs.close();
        QImage image(24,24,QImage::Format_RGB32);image.fill(QColor("#27a88a"));
        const auto picture=scene.files.path()+"/photo.png";
        QVERIFY(image.save(picture));
        QFile page(scene.files.path()+"/download.html");
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("<!doctype html><title>Loading Download Fixture</title>"
                   "<a href='photo.png' download='downloaded-photo.png' style='position:fixed;inset:0' "
                   "onclick=\"document.title='Download clicked'\">Download photo</a>"
                   "<script>addEventListener('load',()=>requestAnimationFrame(()=>{document.title='Download ready'}))</script>");
        page.close();
        // file:// links can navigate despite the download attribute. Exercise
        // the real HTTP download path on an owned loopback server instead.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto *socket=server.nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[&,socket] {
                    auto request=socket->property("request").toByteArray()+socket->readAll();
                    socket->setProperty("request",request);
                    if (!request.contains("\r\n\r\n")) return;
                    const bool png=request.startsWith("GET /photo.png ");
                    const auto body=bytes(png ? picture : page.fileName());
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: "+QByteArray(png ? "image/png" : "text/html")+
                                  "\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                    socket->disconnectFromHost();
                });
            }
        });
        frankenstein::applications::LaunchPlan plan;
        plan.program="/usr/lib/firefox/firefox";
        plan.arguments={"--new-window",QString("http://127.0.0.1:%1/download.html").arg(server.serverPort())};
        QString error;
        QVERIFY2(prepareApplicationAppearance(profile,error),qPrintable(error));
        QVERIFY2(prepareStyledApplication(plan,profile,error),qPrintable(error));
        auto env=nativeApplicationEnvironment(QProcessEnvironment::systemEnvironment(),
                   QString::fromUtf8(scene.compositor.socketName()),profile,
                   externalApplicationPlugins());
        env.insert("MOZ_GTK_TITLEBAR_DECORATION","client"); // Match the production launch path.
        env.insert("MOZ_LOG","Widget:5"); // Disposable fixture diagnostics only.
        silenceFixtureAudio(env,scene.files.path());
        for (const auto &name : {"config","cache","data"}) QVERIFY(QDir().mkpath(profile+"/"+name));
        QProcess browser;
        browser.setProcessEnvironment(env);
        browser.setStandardErrorFile(qEnvironmentVariable("FRANKENSTEIN_EVIDENCE_DIR")+"/browser-save-stderr.txt");
        browser.setStandardOutputFile(QProcess::nullDevice());
        const auto closeBrowser=qScopeGuard([&] {
            if (browser.state()!=QProcess::NotRunning) {browser.kill();browser.waitForFinished(5000);}
        });
        const auto failureCapture=qScopeGuard([&] {
            if(QTest::currentTestFailed()) capture(scene.view,"native-browser-save-failure.png");
        });
        browser.start(plan.program,plan.arguments);
        QVERIFY(browser.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager,"Download ready — Mozilla Firefox").isEmpty(),15000);
        const auto window=first(manager,"Download ready — Mozilla Firefox");
        const auto id=window.value("id").toULongLong();
        auto *client=qobject_cast<QWaylandQuickShellSurfaceItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(id)));
        QVERIFY(client);
        manager.moveWindow(id,130,80);
        manager.resizeWindow(id,800,500);
        const auto sizeReady=[&] {
            for(const auto &entry:manager.snapshot().value("windows").toList())
                if(entry.toMap().value("id").toULongLong()==id)
                    return entry.toMap().value("width").toInt()==800 && entry.toMap().value("height").toInt()==500;
            return false;
        };
        QTRY_VERIFY(sizeReady());
        manager.activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        QVERIFY(!scene.view.grabWindow().isNull());
        const auto point=client->mapToScene(client->mapFromSurface(QPointF(200,250))).toPoint();
        QTest::mouseMove(&scene.view,point);
        QTest::mouseClick(&scene.view,Qt::LeftButton,{},point);
        QTRY_VERIFY(!first(manager,"Download clicked — Mozilla Firefox").isEmpty());
        const bool downloaded=QTest::qWaitFor([&]{return bytes(downloads+"/downloaded-photo.png")==bytes(picture);},5000);
        qInfo()<<"Fixture PNG download completed:"<<downloaded;
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Control,Qt::ControlModifier,29);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_S,Qt::ControlModifier,31);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_S,Qt::ControlModifier,31);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Control,Qt::NoModifier,29);
        const bool dialogOpened=QTest::qWaitFor([&]{return manager.rowCount()>1;},5000);
        qInfo()<<"Browser Save As opened:"<<dialogOpened;
        QVERIFY(capture(scene.view,"native-browser-save-dialog.png"));
        QVERIFY2(downloaded,"The browser did not save the fixture PNG into its configured download folder.");
        QVERIFY2(dialogOpened,"The browser did not open its Save As dialog.");
        // The chooser opening alone is insufficient evidence: accept a name,
        // then inspect the actual page written by Firefox.
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Control,Qt::ControlModifier,29);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_A,Qt::ControlModifier,30);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_A,Qt::ControlModifier,30);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Control,{},29);
        QVERIFY(typeAscii(&scene.view,"savedpage"));
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Return,{},28);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Return,{},28);
        QVERIFY2(QTest::qWaitFor([&]{return bytes(downloads+"/savedpage").contains("Download photo");},5000),
                 "Firefox's Save As did not write the selected page.");
        QTRY_COMPARE(manager.rowCount(),1);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Control,Qt::ControlModifier,29);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_S,Qt::ControlModifier,31);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_S,Qt::ControlModifier,31);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Control,{},29);
        QTRY_VERIFY(manager.rowCount()>1);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Escape,{},1);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Escape,{},1);
        QTRY_COMPARE(manager.rowCount(),1);
        manager.closeWindow(id);
        QTRY_COMPARE(browser.state(),QProcess::NotRunning);
    }
    void firefoxSavesScriptGeneratedDownloadsWithDefaultSettings()
    {
        if (!QFileInfo::exists("/usr/lib/firefox/firefox"))
            QSKIP("This browser workflow needs the installed Firefox.");
        scriptGeneratedSaves(false);
    }
    void chromiumSavesScriptGeneratedDownloadsAndPickerFiles()
    {
        if (!QFileInfo::exists("/usr/bin/chromium"))
            QSKIP("This browser workflow needs the installed Chromium.");
        scriptGeneratedSaves(true);
    }
    void firefoxContextMenuSavesImage()
    {
        if (!QFileInfo::exists("/usr/lib/firefox/firefox"))
            QSKIP("This browser workflow needs the installed Firefox.");
        contextMenuSaves(false);
    }
    void chromiumContextMenuSavesImage()
    {
        if (!QFileInfo::exists("/usr/bin/chromium"))
            QSKIP("This browser workflow needs the installed Chromium.");
        contextMenuSaves(true);
    }
    void puzzleAndScreenshotToolWorkThroughTheDesktop()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        const auto click = [&](const QString &name) {
            if (scene.view.grabWindow().isNull()) return false;
            auto *item = visualItem(scene.view.contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled()) return false;
            QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                             item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
            return true;
        };
        QVERIFY(click("bar-entry-0"));
        QVERIFY(click("desktop-command-puzzle"));
        QTRY_VERIFY(!first(manager,"Puzzle").isEmpty());
        const auto id = first(manager,"Puzzle").value("id").toULongLong();
        const auto window = [&]() -> QVariantMap {
            for (const auto &entry : manager.snapshot().value("windows").toList())
                if (entry.toMap().value("id").toULongLong()==id) return entry.toMap();
            return {};
        };
        QVERIFY(window().value("serverDecorated").toBool());
        QVERIFY(!window().value("canZoom").toBool());
        auto *client = visualItem(scene.view.contentItem(),"client-"+QString::number(id));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Left,{},105);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Left,{},105);
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Right,{},106);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Right,{},106);
        QTRY_VERIFY(window().value("title").toString().contains("move"));
        QVERIFY(capture(scene.view,"native-puzzle.png"));
        screenshotKey(&scene.view);
        auto *panel=scene.view.rootObject()->findChild<QObject *>("screenshot-dialog");
        auto *tool=qobject_cast<ScreenshotService *>(manager.screenshots());
        QVERIFY(panel && tool);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(manager.shellModal());
        QCOMPARE(tool->imageSize(),scene.view.size()*scene.view.devicePixelRatio());
        auto *preview=visualItem(scene.view.contentItem(),"screenshot-preview");
        auto *selection=visualItem(scene.view.contentItem(),"screenshot-selection");
        QVERIFY(preview && selection);
        QTRY_COMPARE(preview->property("status").toInt(),1);
        QVERIFY(!scene.view.grabWindow().isNull());
        const auto paintedWidth=preview->property("paintedWidth").toReal();
        const auto paintedHeight=preview->property("paintedHeight").toReal();
        const auto left=(selection->width()-paintedWidth)/2;
        const auto top=(selection->height()-paintedHeight)/2;
        const auto start=selection->mapToScene(QPointF(left+paintedWidth*0.25,top+paintedHeight*0.25)).toPoint();
        const auto end=selection->mapToScene(QPointF(left+paintedWidth*0.75,top+paintedHeight*0.75)).toPoint();
        QTest::mousePress(&scene.view,Qt::LeftButton,{},start);
        QTest::mouseMove(&scene.view,end,30);
        QTest::mouseRelease(&scene.view,Qt::LeftButton,{},end);
        QVERIFY(tool->selection().width()>0 && tool->selection().width()<tool->imageSize().width());
        const auto croppedSize=tool->selection().size();
        const auto path=scene.files.path()+"/Screenshot.png";
        auto *field=visualItem(scene.view.contentItem(),"screenshot-path");
        QVERIFY(field);
        field->setProperty("text",path);
        QVERIFY(capture(scene.view,"native-screenshot-tool.png"));
        QVERIFY(click("screenshot-save"));
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(QImage(path).size(),croppedSize);
        QVERIFY(tool->preview().isEmpty());
        screenshotKey(&scene.view);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(click("screenshot-cancel"));
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(tool->preview().isEmpty());
        QTRY_VERIFY(client->hasActiveFocus());
        altKey(&scene.view,Qt::Key_Q,16);
        QTRY_COMPARE(manager.rowCount(),0);
    }
    void bluetoothOpensFromApplicationsAndReturnsKeyboardToTheActiveApp()
    {
        // No host BlueZ in this venue: this verifies desktop routing and input,
        // while bluetooth-tests/session qualification use the private fake bus.
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QProcess app;
        app.setProcessEnvironment(scene.childEnvironment());
        app.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Bluetooth"});
        const auto stop = qScopeGuard([&] {
            if (app.state() != QProcess::NotRunning) { app.kill(); app.waitForFinished(3000); }
        });
        QTRY_VERIFY(!first(manager, "Native Probe Bluetooth").isEmpty());
        const auto id = first(manager, "Native Probe Bluetooth").value("id").toULongLong();
        auto *client = visualItem(scene.view.contentItem(), "client-" + QString::number(id));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        auto *browser = scene.view.rootObject()->findChild<QObject *>("application-browser");
        auto *panel = scene.view.rootObject()->findChild<QObject *>("bluetooth-panel");
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(browser && panel && catalog);
        const auto service = panel->property("service").value<QObject *>();
        QSignalSpy errors(&manager, &NativeManager::errorOccurred);
        altKey(&scene.view, Qt::Key_D, 32);
        QTRY_VERIFY(browser->property("opened").toBool());
        QTRY_VERIFY(catalog->ready());
        auto *search = visualItem(scene.view.contentItem(), "application-search");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(typeAscii(&scene.view, "bluetooth"));
        QTRY_COMPARE(catalog->count(), 1);
        QCOMPARE(catalog->item(0).value("name").toString(), QString("Bluetooth"));
        QVERIFY(capture(scene.view, "native-bluetooth-in-applications.png"));
        nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Return, {}, 28);
        nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Return, {}, 28);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(manager.shellModal());
        QVERIFY(!browser->property("visible").toBool());
        QCOMPARE(panel->property("service").value<QObject *>(), service);
        QCOMPARE(manager.rowCount(), 1);
        QVERIFY(capture(scene.view, "native-bluetooth-from-applications.png"));
        QTest::keyClick(&scene.view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QTRY_VERIFY(client->hasActiveFocus());
        QDBusInterface probe("org.frankenstein.NativeProbeBluetooth", "/Probe",
                             "org.frankenstein.NativeProbe");
        QTRY_VERIFY(probe.isValid());
        QVERIFY(typeAscii(&scene.view, "bluetooth works"));
        QTRY_COMPARE(QDBusReply<QString>(probe.call("Text")).value(), QString("bluetooth works"));
        QCOMPARE(errors.count(), 0);
        probe.asyncCall("Finish");
        QTRY_COMPARE(app.state(), QProcess::NotRunning);
        QCOMPARE(app.exitStatus(), QProcess::NormalExit);
        QCOMPARE(app.exitCode(), 0);
    }
    void applicationShortcutsOpenLauncherAndRespectCloseVeto()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        QProcess a, b;
        const auto finish = qScopeGuard([&] {
            for (auto *process : {&a, &b})
                if (process->state() != QProcess::NotRunning)
                {
                    process->kill(); // Disposable qualification clients only.
                    process->waitForFinished(5000);
                }
        });
        a.setProcessEnvironment(scene.childEnvironment());
        b.setProcessEnvironment(scene.childEnvironment());
        const auto program = QCoreApplication::applicationDirPath() + "/native-probe";
        a.start(program, {"ShortcutsA"});
        QVERIFY(a.waitForStarted());
        QTRY_VERIFY(!first(manager, "Native Probe ShortcutsA").isEmpty());
        b.start(program, {"ShortcutsB"});
        QVERIFY(b.waitForStarted());
        QTRY_VERIFY(!first(manager, "Native Probe ShortcutsB").isEmpty());
        const auto id = first(manager, "Native Probe ShortcutsA").value("id").toULongLong();
        QDBusInterface apiA("org.frankenstein.NativeProbeShortcutsA", "/Probe", "org.frankenstein.NativeProbe");
        QDBusInterface apiB("org.frankenstein.NativeProbeShortcutsB", "/Probe", "org.frankenstein.NativeProbe");
        QVERIFY(apiA.isValid() && apiB.isValid());
        manager.activate(id);
        QTRY_VERIFY(first(manager, "Native Probe ShortcutsA").value("active").toBool());
        altKey(&scene.view, Qt::Key_D, 32);
        auto *browser = scene.view.rootObject()->findChild<QObject *>("application-browser");
        QVERIFY(browser);
        QTRY_VERIFY(browser->property("opened").toBool());
        QVERIFY(manager.shellModal());
        auto *search = visualItem(scene.view.contentItem(), "application-search");
        QVERIFY(search);
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(typeAscii(&scene.view, "fixture"));
        QCOMPARE(search->property("text").toString(), QString("fixture"));
        altKey(&scene.view, Qt::Key_Q, 16);
        QCOMPARE(QDBusReply<int>(apiA.call("CloseRequests")).value(), 0);
        QVERIFY(manager.shellModal());
        nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Escape, {}, 1);
        nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Escape, {}, 1);
        QTRY_VERIFY(!manager.shellModal());
        auto *client = visualItem(scene.view.contentItem(), "client-" + QString::number(id));
        QVERIFY(client);
        QTRY_VERIFY(client->hasActiveFocus());
        QCOMPARE(QDBusReply<QString>(apiA.call("Text")).value(), QString());
        QVERIFY(typeAscii(&scene.view, "works"));
        QTRY_COMPARE(QDBusReply<QString>(apiA.call("Text")).value(), QString("works"));
        QVERIFY(QDBusReply<void>(apiA.call("VetoClose", true)).isValid());
        altKey(&scene.view, Qt::Key_Q, 16);
        QTRY_COMPARE(QDBusReply<int>(apiA.call("CloseRequests")).value(), 1);
        QCOMPARE(manager.rowCount(), 2);
        QCOMPARE(QDBusReply<int>(apiB.call("CloseRequests")).value(), 0);
        QVERIFY(QDBusReply<void>(apiA.call("VetoClose", false)).isValid());
        // Super is psn_tch's Command key (D06); Alt+Q above remains their earlier alias.
        commandKey(&scene.view, Qt::Key_Q, 16);
        QTRY_COMPARE(a.state(), QProcess::NotRunning);
        QVERIFY(first(manager, "Native Probe ShortcutsA").isEmpty());
        QVERIFY(b.state() != QProcess::NotRunning);
        QCOMPARE(QDBusReply<int>(apiB.call("CloseRequests")).value(), 0);
        apiB.call("Finish");
        QTRY_COMPARE(b.state(), QProcess::NotRunning);
        QTRY_COMPARE(manager.rowCount(), 0);
        altKey(&scene.view, Qt::Key_Q, 16); // An empty Finder desktop is not logout.
        QVERIFY(manager.desktopActive());
        a.start(program, {"Grouped", "--two-windows"});
        QVERIFY(a.waitForStarted());
        QTRY_COMPARE(manager.rowCount(), 2);
        QCOMPARE(manager.applications().size(), 2); // Finder plus one two-window app.
        QTRY_VERIFY(first(manager, "Native Probe Grouped second").value("active").toBool());
        altKey(&scene.view, Qt::Key_Q, 16);
        QTRY_COMPARE(a.state(), QProcess::NotRunning);
        QTRY_COMPARE(manager.rowCount(), 0);
    }
    void desktopPictureSelectionAppliesAndCancelPreservesIt()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto *settings = qobject_cast<AppearanceSettings *>(scene.manager->appearance());
        QVERIFY(settings && settings->applyDesktopPicture("", "fill"));
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(QColor("#cf3148"));
        const auto path = scene.files.path() + "/Native picture.png";
        QVERIFY(image.save(path));
        const auto click = [&](const QString &name) {
            if (scene.view.grabWindow().isNull()) return false;
            auto *item = visualItem(scene.view.contentItem(), name);
            if (!item || !item->isVisible() || !item->isEnabled()) return false;
            item->parentItem()->ensurePolished();
            QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                             item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
            return true;
        };
        QVERIFY(click("bar-entry-0"));
        QVERIFY(click("desktop-command-control-panels")); // Apple menu > Control Panels
        QVERIFY(click("desktop-command-desktop-picture"));
        auto *panel = scene.view.rootObject()->findChild<QObject *>("desktop-picture-dialog");
        QVERIFY(panel);
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(scene.manager->shellModal());
        auto *folder = visualItem(scene.view.contentItem(), "picture-folder");
        QVERIFY(folder);
        folder->setProperty("text", scene.files.path());
        QVERIFY(click("picture-read-folder"));
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "picture-row-0"));
        QVERIFY(click("picture-row-0"));
        QCOMPARE(panel->property("draftPath").toString(), path);
        auto *preview = visualItem(scene.view.contentItem(), "picture-preview");
        QVERIFY(preview);
        QTRY_COMPARE(preview->property("status").toInt(), 1);
        QVERIFY(click("picture-layout-fit"));
        const auto previewPixel = [&] {
            const auto center = preview->mapToScene(QPointF(preview->width()/2, preview->height()/2));
            const auto dpr = scene.view.devicePixelRatio();
            return scene.view.grabWindow().pixelColor(qRound(center.x()*dpr), qRound(center.y()*dpr));
        };
        QTRY_COMPARE(previewPixel(), QColor("#cf3148"));
        QVERIFY(capture(scene.view, "native-desktop-picture-dialog.png"));
        QVERIFY(click("desktop-picture-dialog-accept"));
        QTRY_VERIFY(!scene.manager->shellModal());
        QCOMPARE(settings->wallpaperPath(), path);
        QCOMPARE(settings->wallpaperLayout(), QString("fit"));
        auto *wallpaper = visualItem(scene.view.contentItem(), "desktop-wallpaper");
        QVERIFY(wallpaper);
        QTRY_COMPARE(wallpaper->property("status").toInt(), 1);
        const auto pixel = [&](int x, int y) {
            const auto capture = scene.view.grabWindow();
            const auto dpr = scene.view.devicePixelRatio();
            return capture.pixelColor(qRound(x*dpr), qRound(y*dpr));
        };
        QTRY_COMPARE(pixel(400, 60), QColor("#708b97"));
        QTRY_COMPARE(pixel(400, 180), QColor("#cf3148"));
        QVERIFY(capture(scene.view, "native-desktop-picture-applied.png"));
        QVERIFY(click("bar-entry-0"));
        QVERIFY(click("desktop-command-control-panels")); // Apple menu > Control Panels
        QVERIFY(click("desktop-command-desktop-picture"));
        QTRY_VERIFY(panel->property("opened").toBool());
        QVERIFY(click("picture-solid"));
        nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Escape, {}, 1);
        nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Escape, {}, 1);
        QTRY_VERIFY(!scene.manager->shellModal());
        QCOMPARE(settings->wallpaperPath(), path);
        AppearanceSettings restored(scene.files.path() + "/profile");
        QCOMPARE(restored.wallpaperPath(), path);
        QCOMPARE(restored.wallpaperLayout(), QString("fit"));
    }
    void firefoxUsesClassicFrameControls_data()
    {
        QTest::addColumn<bool>("normalSettings");
        QTest::newRow("owned profile") << false;
        // psn_tch's own profile under normal application settings: no Platinum styling at all.
        QTest::newRow("normal profile") << true;
    }
    void firefoxUsesClassicFrameControls()
    {
        if (!QFileInfo::exists("/usr/lib/firefox/firefox"))
            QSKIP("Optional native Firefox frame qualification needs installed Firefox.");
        QFETCH(bool, normalSettings);
        NativeScene scene;
        auto &manager = *scene.manager;
        const auto fakeHome = scene.files.path() + "/home";
        QHash<QByteArray, QByteArray> saved;
        const auto restore = qScopeGuard([&] {
            for (auto it = saved.cbegin(); it != saved.cend(); ++it)
                it.value().isNull() ? qunsetenv(it.key().constData()) : qputenv(it.key().constData(), it.value());
        });
        if (normalSettings)
        {
            for (const auto *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
                saved.insert(name, qEnvironmentVariableIsSet(name) ? qgetenv(name) : QByteArray());
            QVERIFY(QDir().mkpath(fakeHome));
            qputenv("HOME", fakeHome.toUtf8());
            qunsetenv("XDG_CONFIG_HOME");
            qunsetenv("XDG_DATA_HOME");
            qunsetenv("XDG_CACHE_HOME");
            manager.setNormalApplicationSettings(true);
        }
        WireTrace focusWire(scene.compositor);
        struct PointerCounts {
            QMap<qint64, QSet<uint>> bindings;
            QMap<qint64, int> buttons;
            QMap<qint64, uint> lastEnter, lastCursor;
        } pointerCounts;
        auto *pointerLogger=wl_display_add_protocol_logger(scene.compositor.display(),
            [](void *data,wl_protocol_logger_type direction,const wl_protocol_logger_message *m) {
                auto &counts=*static_cast<PointerCounts *>(data);
                pid_t pid=0;wl_client_get_credentials(wl_resource_get_client(m->resource),&pid,nullptr,nullptr);
                if(direction==WL_PROTOCOL_LOGGER_REQUEST && QByteArrayView(wl_resource_get_class(m->resource))=="wl_seat" && QByteArrayView(m->message->name)=="get_pointer")
                    counts.bindings[pid].insert(m->arguments[0].n);
                if(direction==WL_PROTOCOL_LOGGER_EVENT && QByteArrayView(wl_resource_get_class(m->resource))=="wl_pointer" && QByteArrayView(m->message->name)=="button")
                    ++counts.buttons[pid];
                if(QByteArrayView(wl_resource_get_class(m->resource))=="wl_pointer") {
                    if(direction==WL_PROTOCOL_LOGGER_EVENT && QByteArrayView(m->message->name)=="enter")
                        counts.lastEnter[pid]=m->arguments[0].u;
                    if(direction==WL_PROTOCOL_LOGGER_REQUEST && QByteArrayView(m->message->name)=="set_cursor")
                        counts.lastCursor[pid]=m->arguments[0].u;
                }
            },&pointerCounts);
        const auto stopPointerLogger=qScopeGuard([&]{wl_protocol_logger_destroy(pointerLogger);});
        QPointer<QQuickItem> diagnosticClient;
        // The owned profile is the session's own; the normal one is an ordinary profile in a
        // disposable home, without the Platinum userChrome or title-bar preference.
        const auto profileDirectory = [&]
        {
            return normalSettings ? fakeHome + "/firefox-profile" : scene.files.path() + "/profile/browsers/firefox";
        };
        const auto failureCapture = qScopeGuard(
            [&]
            {
                if (QTest::currentTestFailed())
                {
                    surfaceGeometry(scene.view, diagnosticClient);
                    capture(scene.view, "native-firefox-frame-failure.png");
                    focusWire.print();
                    const auto addons=QJsonDocument::fromJson(bytes(profileDirectory()+"/extensions.json")).object().value("addons").toArray();
                    for (const auto &addon:addons) {
                        const auto entry=addon.toObject();
                        if (entry.value("active").toBool()) qInfo()<<"Fixture active add-on:"<<entry.value("id").toString();
                    }
                }
            });
        const auto profile = profileDirectory();
        QVERIFY(QDir().mkpath(profile));
        // Only the disposable test profile skips welcome/default-browser prompts.
        QFile preferences(profile + "/prefs.js");
        QVERIFY(preferences.open(QIODevice::WriteOnly));
        preferences.write("user_pref(\"browser.aboutwelcome.enabled\", false);\n"
                          "user_pref(\"browser.shell.checkDefaultBrowser\", false);\n"
                          "user_pref(\"browser.startup.homepage_override.mstone\", \"ignore\");\n"
                          "user_pref(\"datareporting.policy.firstRunURL\", \"\");\n"
                          // System-wide add-ons otherwise open onboarding tabs and
                          // intercept fixture input. This is a disposable test profile.
                          "user_pref(\"extensions.enabledScopes\", 1);\n"
                          "user_pref(\"extensions.autoDisableScopes\", 15);\n");
        preferences.close();
        QVERIFY(QDir().mkpath(scene.files.path() + "/applications"));
        QFile entry(scene.files.path() + "/applications/org.frankenstein.FirefoxFrame.desktop");
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Firefox frame qualification\n"
                    "Exec=/usr/lib/firefox/firefox ");
        if (normalSettings)
            entry.write("--profile \"" + profile.toUtf8() + "\" ");
        entry.write("--new-window \"");
        QFile page(scene.files.path() + "/keyboard.html");
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("<!doctype html><title>Keyboard Fixture</title>"
                   "<input style='position:fixed;left:20px;top:110px;width:540px;height:32px;box-sizing:border-box' "
                   "onfocus=\"document.title='Focused Fixture'\" "
                   "oninput=\"document.title='Typed '+this.value\" "
                   "onkeyup=\"if(event.key==='End')document.title='Caret Fixture '+this.value\">"
                   "<script>const input=document.querySelector('input');"
                   "let pointerInfo='';document.addEventListener('mousedown',event=>{"
                   "pointerInfo=[event.target.tagName,event.clientX,event.clientY,event.button,event.buttons,event.detail,event.altKey,event.ctrlKey,event.metaKey].join(',');"
                   "document.title='Pointer Fixture '+pointerInfo},true);"
                   "input.addEventListener('mouseup',()=>{document.title=document.activeElement===input?'Focused Fixture':"
                   "'Unfocused Fixture '+pointerInfo+' active='+document.activeElement.tagName+' window='+document.hasFocus()});"
                   "input.addEventListener('mousemove',event=>{if(document.hasFocus() && document.activeElement!==input)"
                   "document.title='Hover Fixture INPUT '+event.clientX+','+event.clientY});"
                   "function ready(){requestAnimationFrame(()=>{if(document.hasFocus() && "
                   "!input.value && document.activeElement!==input)document.title='Ready Fixture '+innerWidth+' height='+innerHeight})}"
                   "addEventListener('load',ready);addEventListener('resize',ready);addEventListener('focus',ready)</script>");
        page.close();
        entry.write(QUrl::fromLocalFile(page.fileName()).toEncoded().replace("%", "%%"));
        entry.write("\"\n");
        entry.close();
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(catalog);
        catalog->refresh();
        QVERIFY(scene.show());
        auto *appearance = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(appearance && appearance->apply(0, true));
        if (!appearance->wallpaper().isEmpty())
        {
            auto *wallpaper = visualItem(scene.view.contentItem(), "desktop-wallpaper");
            QVERIFY(wallpaper);
            QTRY_COMPARE(wallpaper->property("status").toInt(), 1); // Image.Ready.
        }
        QTRY_VERIFY(catalog->ready() && !catalog->busy());
        manager.launchApplication("org.frankenstein.FirefoxFrame.desktop");
        QTRY_VERIFY_WITH_TIMEOUT(manager.rowCount() > 0, 15000);
        const auto browserWindow = [&]() -> QVariantMap
        {
            for (const auto &value : manager.snapshot().value("windows").toList())
                if (value.toMap().value("classicBrowserFrame").toBool())
                    return value.toMap();
            return {};
        };
        QTRY_VERIFY_WITH_TIMEOUT(!browserWindow().isEmpty(), 5000);
        const auto id = browserWindow().value("id").toULongLong();
        auto *client = qobject_cast<QWaylandQuickShellSurfaceItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
        QVERIFY(client);
        diagnosticClient = client;
        KeyCounter keyCounter;
        client->installEventFilter(&keyCounter);
        KeyCounter rootInput;
        scene.view.installEventFilter(&rootInput);
        int wireKeys = 0;
        auto *keyLogger = wl_display_add_protocol_logger(scene.compositor.display(),
            [](void *data, wl_protocol_logger_type direction, const wl_protocol_logger_message *m)
            {
                if (direction == WL_PROTOCOL_LOGGER_EVENT &&
                    QByteArrayView(wl_resource_get_class(m->resource)) == "wl_keyboard" &&
                    QByteArrayView(m->message->name) == "key")
                    ++*static_cast<int *>(data);
            }, &wireKeys);
        const auto stopKeyLogger = qScopeGuard([&] { wl_protocol_logger_destroy(keyLogger); });
        const auto browserPid = liveSurfaceClientPid(client->surface());
        QVERIFY(browserPid > 0);
        QVERIFY(browserWindow().value("serverDecorated").toBool());
        QVERIFY(browserWindow().value("hasCloseBox").toBool());
        manager.moveWindow(id, 130, 80);
        manager.resizeWindow(id, 800, 500);
        QTRY_COMPARE(browserWindow().value("width").toInt(), 800);
        auto *frame = visualItem(scene.view.contentItem(), "frame-" + QString::number(id));
        QVERIFY(frame);
        auto *xdg = qobject_cast<QWaylandXdgSurface *>(client->shellSurface());
        QVERIFY(xdg);
        qInfo() << "Firefox initial window geometry:" << xdg->windowGeometry()
                << "surface size:" << client->surface()->destinationSize();
        const auto aligned = [&]
        {
            const auto expected =
                frame->mapRectToScene(QRectF(3, 23, browserWindow().value("width").toInt(),
                                             browserWindow().value("height").toInt()));
            const auto actual = client->mapRectToScene(client->boundingRect());
            if (xdg->windowGeometry().topLeft() != QPoint() ||
                qAbs(actual.left() - expected.left()) > 0.5 ||
                qAbs(actual.top() - expected.top()) > 0.5 ||
                qAbs(actual.right() - expected.right()) > 0.5 ||
                qAbs(actual.bottom() - expected.bottom()) > 0.5)
                return false;
            const auto within = [&](auto &&self, QQuickItem *parent) -> bool
            {
                for (auto *child : parent->childItems())
                {
                    if (auto *item = qobject_cast<QWaylandQuickItem *>(child);
                        item && item->isVisible() && item->surface() &&
                        item->surface()->hasContent() &&
                        item->surface()->destinationSize().width() > 0 &&
                        item->surface()->destinationSize().height() > 0 &&
                        !expected.adjusted(-0.5, -0.5, 0.5, 0.5)
                             .contains(item->mapRectToScene(item->boundingRect())))
                        return false;
                    if (!self(self, child))
                        return false;
                }
                return true;
            };
            return within(within, client);
        };
        QTRY_VERIFY(aligned());
        // The head title can arrive before the input exists. Wait for the
        // fixture's load/frame signal instead of clicking an unfinished page.
        manager.activate(id);
        QTRY_VERIFY(client->hasActiveFocus());
        // Seat focus and an acknowledged buffer size can precede DOM focus and
        // the content process's new layout. The fixture confirms both explicitly.
        QTRY_VERIFY(browserWindow().value("title").toString().startsWith("Ready Fixture 800"));
        QVERIFY(!scene.view.grabWindow().isNull());
        const auto readyTitle=browserWindow().value("title").toString();
        const auto contentHeight=readyTitle.section("height=",1).section(' ',0,0).toInt();
        QVERIFY(contentHeight>0 && contentHeight<=500);
        // A normal single-line form control, at its DOM center. Account for
        // the browser toolbar using the fixture's reported viewport height.
        const auto inputSurfacePoint=QPointF(290,126+500-contentHeight);
        const auto firstInputPoint=client->mapToScene(client->mapFromSurface(inputSurfacePoint)).toPoint();
        QTest::mouseMove(&scene.view,firstInputPoint-QPoint(0,5));
        // Enter and button in one synthetic burst can precede GTK's processing
        // of its crossing event. Its cursor request acknowledges that exact
        // enter serial; then move onto the control and issue one real click.
        QTRY_VERIFY(pointerCounts.lastEnter.value(browserPid)!=0 &&
                    pointerCounts.lastCursor.value(browserPid)==pointerCounts.lastEnter.value(browserPid));
        QTest::mouseMove(&scene.view,firstInputPoint);
        // GTK's cursor request only acknowledges its toplevel crossing event. The content
        // process can still be processing hover/layout; require the real target's DOM motion
        // acknowledgement before the unchanged single press/release and focus assertion.
        QTRY_VERIFY(browserWindow().value("title").toString().startsWith("Hover Fixture INPUT "));
        QTest::mousePress(&scene.view,Qt::LeftButton,{},firstInputPoint);
        QTest::qWait(20);
        QTest::mouseRelease(&scene.view,Qt::LeftButton,{},firstInputPoint);
        qInfo()<<"Browser pointer bindings"<<pointerCounts.bindings.value(browserPid).size()<<"wire buttons"<<pointerCounts.buttons.value(browserPid)<<"active"<<browserWindow().value("active")<<"input enabled"<<client->inputEventsEnabled()<<"client press/double"<<keyCounter.mousePresses<<keyCounter.doubleClicks<<"root press/double"<<rootInput.mousePresses<<rootInput.doubleClicks;
        const bool pointerFocused=QTest::qWaitFor([&]{return browserWindow().value("title").toString().startsWith("Focused Fixture");},5000);
        if (!pointerFocused) {
            qInfo()<<"Firefox DOM state after pointer:"<<browserWindow().value("title")
                   <<"activated:"<<xdg->toplevel()->activated()
                   <<"seat root:"<<(scene.compositor.defaultSeat()->keyboardFocus()==client->surface());
            nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_Tab,{},15);
            nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_Tab,{},15);
            const bool tabFocused=QTest::qWaitFor([&]{return browserWindow().value("title").toString().startsWith("Focused Fixture");},1000);
            qInfo()<<"Firefox DOM focus after diagnostic Tab:"<<tabFocused<<browserWindow().value("title");
        }
        QVERIFY2(pointerFocused,"The single pointer click did not focus the fixture input.");
        QTRY_COMPARE(scene.compositor.defaultSeat()->keyboardFocus(), client->surface());
        qInfo() << "Browser input target before typing: root-active-focus" << client->hasActiveFocus()
                << "active-class" << (scene.view.activeFocusItem()
                    ? scene.view.activeFocusItem()->metaObject()->className() : "none");
        QVERIFY(typeAscii(&scene.view, "platinum"));
        qInfo() << "Browser input target after typing: root-active-focus" << client->hasActiveFocus()
                << "keyboard-root" << (scene.compositor.defaultSeat()->keyboardFocus() == client->surface())
                << "active-class" << (scene.view.activeFocusItem()
                    ? scene.view.activeFocusItem()->metaObject()->className() : "none");
        qInfo() << "Browser key counts: item presses" << keyCounter.presses << "wire events" << wireKeys;
        QTRY_VERIFY(browserWindow().value("title").toString().startsWith("Typed platinum"));
        // psn_tch (2026-10-20): clicking away and back moved their Firefox. Neither the frame nor
        // the page inside it may move when the window loses or regains focus.
        const auto framePosition = frame->position();
        const auto contentArea = client->mapRectToScene(client->boundingRect());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, QPoint(30, 690));
        QVERIFY(manager.desktopActive());
        QVERIFY(!scene.compositor.defaultSeat()->keyboardFocus());
        QTest::qWait(500); // The browser redraws itself as inactive.
        QVERIFY2(aligned() && client->mapRectToScene(client->boundingRect()) == contentArea,
                 qPrintable(QString("inactive geometry %1,%2").arg(xdg->windowGeometry().x()).arg(xdg->windowGeometry().y())));
        const auto inputPoint = client->mapToScene(client->mapFromSurface(inputSurfacePoint)).toPoint();
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, inputPoint); // classic activation click
        QTRY_VERIFY(!manager.desktopActive());
        QTest::qWait(500);
        QVERIFY(aligned() && client->mapRectToScene(client->boundingRect()) == contentArea);
        QCOMPARE(frame->position(), framePosition);
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, inputPoint);
        QTRY_VERIFY(browserWindow().value("title").toString().startsWith("Focused Fixture"));
        QTRY_COMPARE(scene.compositor.defaultSeat()->keyboardFocus(),client->surface());
        // Rapid activation/clicks may select the existing word. Confirm client
        // focus and place the caret explicitly before testing appended input.
        nativeKeyEvent(&scene.view,QEvent::KeyPress,Qt::Key_End,{},107);
        nativeKeyEvent(&scene.view,QEvent::KeyRelease,Qt::Key_End,{},107);
        QTRY_VERIFY(browserWindow().value("title").toString().startsWith("Caret Fixture platinum"));
        QVERIFY(typeAscii(&scene.view, " works"));
        QTRY_VERIFY(browserWindow().value("title").toString().startsWith("Typed platinum works"));
        const auto clickBox = [&](const QString &kind)
        {
            auto *item = visualItem(scene.view.contentItem(), kind + QString::number(id));
            if (!item || !item->isVisible())
                return false;
            QTest::mouseClick(
                &scene.view, Qt::LeftButton, {},
                item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
            return true;
        };
        QVERIFY(capture(scene.view, "native-firefox-classic-frame.png"));
        QVERIFY(clickBox("shade-box-"));
        QTRY_VERIFY(browserWindow().value("collapsed").toBool());
        QVERIFY(clickBox("shade-box-"));
        QTRY_VERIFY(!browserWindow().value("collapsed").toBool());
        const int originalWidth = browserWindow().value("width").toInt();
        QVERIFY(clickBox("zoom-box-"));
        QTRY_VERIFY(browserWindow().value("width").toInt() != originalWidth);
        QVERIFY(clickBox("zoom-box-"));
        QTRY_COMPARE(browserWindow().value("width").toInt(), originalWidth);
        QTRY_VERIFY(aligned());
        QVERIFY(clickBox("close-box-"));
        QTRY_COMPARE(manager.rowCount(), 0);
        QTRY_VERIFY(!QFileInfo::exists(QString("/proc/%1").arg(browserPid)));
    }
#ifdef FRANKENSTEIN_GTK_PROBES
    void launchedGtkApplicationsUsePlatinumControlsAndRetainInput_data()
    {
        QTest::addColumn<bool>("normalSettings");
        QTest::newRow("private") << false;
        QTest::newRow("normal") << true;
    }
    void launchedGtkApplicationsUsePlatinumControlsAndRetainInput()
    {
        // Private: the session profile's folders. Normal (the installed default): the user's own
        // folders, here a disposable fake home, with the Platinum look from a private GTK theme
        // and nothing written into the user's GTK configuration.
        QFETCH(bool, normalSettings);
        NativeScene scene;
        auto &manager = *scene.manager;
        const auto fakeHome = scene.files.path() + "/home";
        QHash<QByteArray, QByteArray> saved;
        const auto restore = qScopeGuard([&] {
            for (auto it = saved.cbegin(); it != saved.cend(); ++it)
                it.value().isNull() ? qunsetenv(it.key().constData()) : qputenv(it.key().constData(), it.value());
        });
        if (normalSettings)
        {
            for (const auto *name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
                saved.insert(name, qEnvironmentVariableIsSet(name) ? qgetenv(name) : QByteArray());
            QVERIFY(QDir().mkpath(fakeHome + "/.config/gtk-3.0"));
            qputenv("HOME", fakeHome.toUtf8());
            qunsetenv("XDG_CONFIG_HOME");
            qunsetenv("XDG_DATA_HOME");
            qunsetenv("XDG_CACHE_HOME");
            manager.setNormalApplicationSettings(true);
        }
        const auto expectedConfig = normalSettings ? fakeHome + "/.config" : scene.files.path() + "/profile/config";
        QPointer<QQuickItem> diagnosticClient;
        const auto failureCapture = qScopeGuard(
            [&]
            {
                if (QTest::currentTestFailed())
                {
                    surfaceGeometry(scene.view, diagnosticClient);
                    capture(scene.view, "native-gtk-input-failure.png");
                }
            });
        QVERIFY(QDir().mkpath(scene.files.path() + "/applications"));
        for (const auto &version : {"3", "4"})
        {
            QFile file(scene.files.path() + "/applications/org.frankenstein.GtkProbe" + version +
                       ".desktop");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(
                (QString("[Desktop Entry]\nType=Application\nName=GTK %1 style probe\nExec=\"")
                     .arg(version) +
                 QCoreApplication::applicationDirPath() + "/gtk-appearance-probe" + version +
                 "\"\n")
                    .toUtf8());
        }
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(catalog);
        catalog->refresh();
        QVERIFY(scene.show());
        QTRY_VERIFY(catalog->ready() && !catalog->busy());
        for (const auto &version : {"3", "4"})
        {
            const auto title = QString("GTK %1 Platinum Probe").arg(version);
            manager.launchApplication(QString("org.frankenstein.GtkProbe%1.desktop").arg(version));
            QTRY_VERIFY_WITH_TIMEOUT(!first(manager, title).isEmpty(), 10000);
            const auto id = first(manager, title).value("id").toULongLong();
            QDBusInterface probe(QString("org.frankenstein.GtkProbe%1").arg(version), "/Probe",
                                 "org.frankenstein.GtkProbe");
            QTRY_VERIFY(probe.isValid());
            const auto state = [&] { return QDBusReply<QVariantMap>(probe.call("State")).value(); };
            auto *client = qobject_cast<QWaylandQuickItem *>(
                visualItem(scene.view.contentItem(), "client-" + QString::number(id)));
            QVERIFY(client);
            diagnosticClient = client;
            const auto point = [&](QPointF local)
            { return client->mapToScene(client->mapFromSurface(local)).toPoint(); };
            QTest::mouseClick(&scene.view, Qt::LeftButton, {}, point({80, 63}));
            QTRY_VERIFY(state().value("entryFocused").toBool());
            QVERIFY(typeAscii(&scene.view, "Classic controls and native input"));
            QTRY_COMPARE(state().value("text").toString(),
                         QString("Classic controls and native input"));
            QTest::mouseClick(&scene.view, Qt::LeftButton, {}, point({100, 111}));
            QTRY_COMPARE(state().value("clicks").toInt(), 1);
            QTest::mouseClick(&scene.view, Qt::LeftButton, {}, point({24, 158}));
            QTRY_VERIFY(state().value("checked").toBool());
            const auto font = state().value("font").toString();
            qInfo().noquote() << "GTK" << version << "computed application font:" << font;
            QVERIFY(font.contains("Charcoal") || font.contains("DejaVu Sans"));
            QCOMPARE(QFileInfo(state().value("configHome").toString()).absoluteFilePath(),
                     QFileInfo(expectedConfig).absoluteFilePath());
            if (normalSettings)
            {
                QVERIFY(QDir(fakeHome + "/.config/gtk-3.0").isEmpty());
                QVERIFY(!QFileInfo::exists(fakeHome + "/.config/gtk-4.0/gtk.css"));
                QVERIFY(QFileInfo::exists(scene.files.path() + "/profile/share/themes/PlatinumReality/gtk-" +
                                          QString(version) + ".0/gtk.css"));
            }
            QVERIFY(capture(scene.view, QString("native-gtk%1-appearance%2.png").arg(version, normalSettings ? "-normal" : "")));
            const QDBusMessage reply = probe.call("Finish");
            QVERIFY(reply.type() != QDBusMessage::ErrorMessage);
            QTRY_COMPARE(manager.rowCount(), 0);
        }
    }
#endif
    void desktopFilesDropIntoExactFinderWindowsAndChildFolders()
    {
        NativeScene scene;
        auto &manager = *scene.manager;
        auto *desktop = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(desktop);
        const auto left = scene.files.path() + "/One/Folder";
        const auto right = scene.files.path() + "/Two/Folder";
        QVERIFY(QDir().mkpath(left));
        QVERIFY(QDir().mkpath(right + "/Inside"));
        const QStringList names{"Move inside.txt", "Copy inside.txt", "Collision.txt",
                                "Cancelled.txt", "Chrome.txt"};
        for (const auto &name : names)
        {
            QFile file(desktop->directory() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name.toUtf8());
        }
        {
            QFile file(right + "/Collision.txt");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("destination preserved");
        }
        desktop->refresh();
        QVERIFY(scene.show());
        // Place the fixtures with real pointer drags before opening windows.
        int column = 0;
        for (const auto &name : names)
        {
            QTRY_VERIFY(visualItem(scene.view.contentItem(), "desktop-entry-" + name));
            QPointer<QQuickItem> icon =
                visualItem(scene.view.contentItem(), "desktop-entry-" + name);
            const auto start = icon->mapToScene(QPointF(50, 20)).toPoint();
            const QPoint end(200 + 150 * column++, 620);
            QTest::mousePress(&scene.view, Qt::LeftButton, {}, start);
            QTest::mouseMove(&scene.view, end, 40);
            QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, end);
            QTRY_VERIFY(!desktop->dragging());
        }
        const auto service =
            "org.frankenstein.Finder.p" +
            QString::fromLatin1(
                QCryptographicHash::hash(
                    (scene.files.path() + "\n" + scene.files.path() + "/profile").toUtf8(),
                    QCryptographicHash::Sha256)
                    .toHex()
                    .left(24));
        manager.launchFinder(left);
        QTRY_COMPARE(manager.rowCount(), 1);
        QDBusInterface finder(service, "/Finder", "org.frankenstein.Finder");
        QTRY_VERIFY(finder.isValid());
        const auto state = [&](const QString &path)
        { return QDBusReply<QVariantMap>(finder.call("DirectoryState", path)).value(); };
        const auto windowId = [&](const QString &path) -> qulonglong
        {
            const auto wireId = state(path).value("surfaceId").toUInt();
            if (!wireId)
                return 0;
            for (const auto &value : manager.snapshot().value("windows").toList())
                if (value.toMap().value("surfaceId").toUInt() == wireId)
                    return value.toMap().value("id").toULongLong();
            return 0;
        };
        QTRY_VERIFY(state(left).value("loaded").toBool() && windowId(left));
        const auto leftId = windowId(left);
        manager.moveWindow(leftId, 45, 65);
        manager.resizeWindow(leftId, 370, 300);
        manager.launchFinder(right);
        QTRY_COMPARE(manager.rowCount(), 2);
        QTRY_VERIFY(state(right).value("loaded").toBool() && windowId(right));
        const auto rightId = windowId(right);
        QVERIFY(rightId != leftId);
        manager.moveWindow(rightId, 600, 65);
        manager.resizeWindow(rightId, 370, 300);
        for (auto id : {leftId, rightId})
            QTRY_VERIFY(
                [&]
                {
                    for (const auto &value : manager.snapshot().value("windows").toList())
                    {
                        const auto w = value.toMap();
                        if (w.value("id").toULongLong() == id)
                            return w.value("width").toInt() == 370 &&
                                   w.value("height").toInt() == 300;
                    }
                    return false;
                }());
        auto *rightClient = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(rightId)));
        QVERIFY(rightClient);
        const auto rootPoint =
            rightClient->mapToScene(rightClient->mapFromSurface({140, 200})).toPoint();
        QPoint childPoint;
        for (const auto &value : qdbus_cast<QVariantList>(state(right).value("entries")))
        {
            const auto entry = qdbus_cast<QVariantMap>(value);
            if (entry.value("name").toString() == "Inside")
                childPoint =
                    rightClient
                        ->mapToScene(rightClient->mapFromSurface(
                            {entry.value("x").toReal() + entry.value("width").toReal() / 2,
                             entry.value("y").toReal() + entry.value("height").toReal() / 2}))
                        .toPoint();
        }
        QVERIFY(!childPoint.isNull());
        // Same process, different bus connection: a claimed PID is insufficient.
        {
            auto untrusted =
                QDBusConnection::connectToBus(QDBusConnection::SessionBus, "untrusted-folder-drop");
            const auto disconnect =
                qScopeGuard([] { QDBusConnection::disconnectFromBus("untrusted-folder-drop"); });
            QVERIFY(untrusted.isConnected());
            QDBusInterface denied(service, "/Finder", "org.frankenstein.Finder", untrusted);
            const QDBusReply<QString> result =
                denied.call("DropDirectory", state(right).value("surfaceId").toUInt(), 140, 200);
            QVERIFY(result.isValid());
            QVERIFY(result.value().isEmpty());
        }
        for (const auto &name : names)
        {
            const bool copy = name == "Copy inside.txt";
            const bool cancel = name == "Cancelled.txt";
            const bool chrome = name == "Chrome.txt";
            const bool conflict = name == "Collision.txt";
            QPointer<QQuickItem> icon =
                visualItem(scene.view.contentItem(), "desktop-entry-" + name);
            QVERIFY(icon);
            const auto start = icon->mapToScene(QPointF(50, 20)).toPoint();
            const auto end = chrome ? QPoint(750, 76) : copy ? childPoint : rootPoint;
            if (copy)
                nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier, 56);
            QTest::mousePress(&scene.view, Qt::LeftButton, copy ? Qt::AltModifier : Qt::NoModifier,
                              start);
            QTest::mouseMove(&scene.view, end, 40);
            QTRY_VERIFY(desktop->dragging());
            QTRY_COMPARE(manager.desktopDropWindow(), chrome ? qulonglong(0) : rightId);
            if (cancel)
                QTest::keyClick(&scene.view, Qt::Key_Escape);
            QTest::mouseRelease(&scene.view, Qt::LeftButton,
                                copy ? Qt::AltModifier : Qt::NoModifier, end);
            if (copy)
                nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Alt, {}, 56);
            QTRY_VERIFY(!desktop->dragging() && !desktop->busy());
            if (conflict)
            {
                QTRY_VERIFY(manager.shellModal());
                QCOMPARE(bytes(right + '/' + name), QByteArray("destination preserved"));
                QTest::keyClick(&scene.view, Qt::Key_Escape);
                QTRY_VERIFY(!manager.shellModal());
            }
            else if (!cancel && !chrome)
                QCOMPARE(bytes(right + (copy ? "/Inside/" : "/") + name), name.toUtf8());
            QCOMPARE(QFileInfo::exists(desktop->directory() + '/' + name),
                     copy || cancel || chrome || conflict);
            if (cancel || chrome)
            {
                QVERIFY(!QFileInfo::exists(right + '/' + name));
                QTRY_VERIFY(
                    (icon = visualItem(scene.view.contentItem(), "desktop-entry-" + name)) &&
                    icon->mapToScene(QPointF(50, 20)).toPoint() == start);
            }
            QCOMPARE(state(left).value("items").toInt(), 0);
        }
        QTRY_COMPARE(state(right).value("items").toInt(), 3);
        QVERIFY(capture(scene.view, "native-desktop-to-folder.png"));
        // Add two well-separated fixtures below the windows. Selection and the
        // group transfer then use real shell pointer input, not model selection.
        int groupX = 200;
        for (const auto &name : {"Group one.txt", "Group two.txt"})
        {
            {
                QFile file(desktop->directory() + '/' + name);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(name);
            }
            desktop->refresh();
            bool placed = false;
            for (int row = 0; row < desktop->rowCount(); ++row)
            {
                const auto index = desktop->index(row);
                if (desktop->data(index, DesktopItems::NameRole) != name)
                    continue;
                desktop->clearSelection();
                desktop->select(desktop->data(index, DesktopItems::KeyRole).toString());
                desktop->moveSelected(groupX - desktop->data(index, DesktopItems::XRole).toInt(),
                                      450 - desktop->data(index, DesktopItems::YRole).toInt());
                placed = true;
                groupX += 180;
                break;
            }
            QVERIFY(placed);
        }
        desktop->clearSelection();
        QTest::mousePress(&scene.view, Qt::LeftButton, {}, {180, 430});
        QTest::mouseMove(&scene.view, {500, 550}, 40);
        QTRY_VERIFY(desktop->selecting());
        QTRY_COMPARE(desktop->selectionCount(), 2);
        QVERIFY(capture(scene.view, "native-desktop-marquee.png"));
        QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, {500, 550});
        QVERIFY(!desktop->selecting());
        QTest::mousePress(&scene.view, Qt::LeftButton, {}, {140, 420});
        QTest::mouseMove(&scene.view, {160, 560}, 40);
        QCOMPARE(desktop->selectionCount(), 0);
        QTest::keyClick(&scene.view, Qt::Key_Escape);
        QCOMPARE(desktop->selectionCount(), 2);
        QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, {160, 560});
        auto *groupSource = visualItem(scene.view.contentItem(), "desktop-entry-Group one.txt");
        QVERIFY(groupSource);
        const auto groupStart = groupSource->mapToScene(QPointF(50, 20)).toPoint();
        QTest::mousePress(&scene.view, Qt::LeftButton, {}, groupStart);
        QCOMPARE(desktop->selectionCount(), 2);
        QTest::mouseMove(&scene.view, rootPoint, 40);
        QTRY_COMPARE(manager.desktopDropWindow(), rightId);
        QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, rootPoint);
        QTRY_VERIFY(!desktop->dragging() && !desktop->busy());
        for (const auto &name : {"Group one.txt", "Group two.txt"})
        {
            QCOMPARE(bytes(right + '/' + name), QByteArray(name));
            QVERIFY(!QFileInfo::exists(desktop->directory() + '/' + name));
        }
        QTRY_COMPARE(state(right).value("items").toInt(), 5);
        QCOMPARE(state(left).value("items").toInt(), 0);
        manager.closeWindow(leftId);
        manager.closeWindow(rightId);
        QTRY_COMPARE(manager.rowCount(), 0);
    }
    void finderDragsReachDesktopAndBackgroundFoldersAndRightClickMenus()
    {
        NativeScene scene;
        auto &manager = *scene.manager;
        auto *desktop = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(desktop);
        const auto from = scene.files.path() + "/Source", to = scene.files.path() + "/Destination";
        QVERIFY(QDir().mkpath(from));
        QVERIFY(QDir().mkpath(to));
        for (const auto &name :
             {"Moved.txt", "Copied.txt", "Cancelled.txt", "Conflict.txt", "Between.txt"})
        {
            QFile file(from + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        {
            QFile file(desktop->directory() + "/Conflict.txt");
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("destination preserved");
        }
        QVERIFY(scene.show());
        QTRY_VERIFY_WITH_TIMEOUT(manager.snapshot().value("dropReceiverReady").toBool(), 10000);
        manager.launchFinder(from);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Source").isEmpty(), 10000);
        const auto sourceId = first(manager, "Source").value("id").toULongLong();
        manager.moveWindow(sourceId, 45, 65);
        manager.resizeWindow(sourceId, 370, 300);
        const auto service =
            "org.frankenstein.Finder.p" +
            QString::fromLatin1(
                QCryptographicHash::hash(
                    (scene.files.path() + "\n" + scene.files.path() + "/profile").toUtf8(),
                    QCryptographicHash::Sha256)
                    .toHex()
                    .left(24));
        QDBusInterface finder(service, "/Finder", "org.frankenstein.Finder");
        const auto failureEvidence = qScopeGuard(
            [&]
            {
                if (QTest::currentTestFailed())
                {
                    capture(scene.view, "native-folder-drop-failure.png");
                    qInfo().noquote() << "Drop failure state:"
                                      << QJsonDocument::fromVariant(manager.snapshot())
                                             .toJson(QJsonDocument::Compact);
                }
            });
        QTRY_VERIFY(finder.isValid());
        const auto state = [&](const QString &directory)
        { return QDBusReply<QVariantMap>(finder.call("DirectoryState", directory)).value(); };
        QTRY_VERIFY(state(from).value("loaded").toBool());
        QTRY_COMPARE(first(manager, "Source").value("width").toInt(), 370);
        manager.launchFinder(to);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Destination").isEmpty(), 10000);
        const auto destinationId = first(manager, "Destination").value("id").toULongLong();
        manager.moveWindow(destinationId, 600, 65);
        manager.resizeWindow(destinationId, 370, 300);
        QTRY_VERIFY(state(to).value("loaded").toBool());
        QTRY_COMPARE(first(manager, "Destination").value("width").toInt(), 370);
        const auto filePoint = [&](const QString &name)
        {
            auto *client = qobject_cast<QWaylandQuickItem *>(
                visualItem(scene.view.contentItem(), "client-" + QString::number(sourceId)));
            if (!client)
                return QPoint(-1, -1);
            const auto entries = qdbus_cast<QVariantList>(state(from).value("entries"));
            for (const auto &value : entries)
            {
                const auto item = qdbus_cast<QVariantMap>(value);
                if (item.value("name").toString() != name)
                    continue;
                const QPointF local(item.value("x").toReal() + item.value("width").toReal() / 2,
                                    item.value("y").toReal() + item.value("height").toReal() / 2);
                return client->mapToScene(client->mapFromSurface(local)).toPoint();
            }
            return QPoint(-1, -1);
        };
        // Native menu in an inactive folder is a deliberate modern adaptation.
        QTRY_VERIFY(filePoint("Copied.txt").x() > 0);
        const auto contextPoint = filePoint("Copied.txt");
        QTest::mouseClick(&scene.view, Qt::RightButton, {}, contextPoint);
        QTRY_VERIFY(state(from).value("contextMenuVisible").toBool());
        QVERIFY(capture(scene.view, "native-finder-context.png"));
        nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Escape, {}, 1);
        nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Escape, {}, 1);
        QTRY_VERIFY(!state(from).value("contextMenuVisible").toBool());
        auto denied = QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                     "org.frankenstein.Desktop", "DropFiles");
        denied << QStringList{from + "/Moved.txt"} << QString();
        QDBusPendingCallWatcher denial(QDBusConnection::sessionBus().asyncCall(denied));
        QTRY_VERIFY(denial.isFinished());
        const QDBusPendingReply<bool> deniedReply = denial;
        QVERIFY(!deniedReply.isError());
        QVERIFY(!deniedReply.value());
        int dropIndex = 0;
        for (const auto &name :
             {QString("Moved.txt"), QString("Copied.txt"), QString("Cancelled.txt"),
              QString("Conflict.txt"), QString("Between.txt")})
        {
            manager.activate(sourceId);
            if (name == "Between.txt")
            {
                auto *broker = qobject_cast<MenuBroker *>(manager.menuBroker());
                QVERIFY(broker);
                QTRY_VERIFY(!broker->busy() && command(broker->menus(), "as List") >= 0);
                const auto listCommand = command(broker->menus(), "as List");
                auto *bar = visualItem(scene.view.contentItem(), "bar-entry-3");
                QVERIFY(bar);
                bar->parentItem()->ensurePolished();
                QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                                  bar->mapToScene(QPointF(20, 13)).toPoint());
                // Opening an exported menu starts asynchronous AboutToShow and
                // GetLayout calls. Its initially visible rows are disabled and
                // may be replaced until that preparation completes.
                QTRY_VERIFY(!broker->busy());
                QPointer<QQuickItem> item;
                QTRY_VERIFY((item = visualItem(scene.view.contentItem(),
                                               "menu-command-" + QString::number(listCommand))) &&
                            item->isVisible() && item->isEnabled() && item->width() > 100);
                item->parentItem()->ensurePolished();
                QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                                  item->mapToScene(QPointF(60, 12)).toPoint());
                QTRY_VERIFY(!manager.menuOpen());
                QTRY_VERIFY(state(from).value("listView").toBool());
            }
            // Drag from the active Source window: a first click on an inactive window only activates
            // it. Finder's own activation requests can arrive late in a slow (sanitizer) run, the
            // test's blocking D-Bus calls holding them up: closing its context menu, Qt re-activates
            // the window it last saw active, which may be Destination. Source must stay active.
            manager.activate(sourceId);
            QTRY_VERIFY(first(manager, "Source").value("active").toBool());
            for (int stable = 0; stable < 10; ++stable)
            {
                QTest::qWait(50);
                if (!first(manager, "Source").value("active").toBool())
                {
                    manager.activate(sourceId);
                    QTRY_VERIFY(first(manager, "Source").value("active").toBool());
                    stable = -1;
                }
            }
            QTRY_VERIFY(filePoint(name).x() > 0);
            const auto start = filePoint(name);
            const bool copy = name == "Copied.txt", cancel = name == "Cancelled.txt",
                       conflict = name == "Conflict.txt", between = name == "Between.txt";
            const QPoint end = between ? QPoint(750, 225) : QPoint(240 + 130 * dropIndex++, 610);
            QSignalSpy errors(&manager, &NativeManager::errorOccurred);
            qInfo().noquote() << "Testing file drop:" << name;
            if (copy)
                nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier, 56);
            QTest::mousePress(&scene.view, Qt::LeftButton, copy ? Qt::AltModifier : Qt::NoModifier,
                              start);
            // Which item took the press, for a drag that never starts.
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            const auto *grabber = scene.view.mouseGrabberItem();
            QT_WARNING_POP
            const QString pressedOn = grabber ? QString::fromLatin1(grabber->metaObject()->className()) + " '" +
                                                    grabber->objectName() + "'" : QString("no item");
            QString sourceInput;
            if (auto *item = qobject_cast<QWaylandQuickItem *>(
                    visualItem(scene.view.contentItem(), "client-" + QString::number(sourceId))))
                sourceInput = QString(" (Source item: enabled %1, input %2, region contains %3; menu open %4, shell modal %5)")
                                  .arg(item->isEnabled()).arg(item->inputEventsEnabled())
                                  .arg(item->inputRegionContains(item->mapFromScene(QPointF(start))))
                                  .arg(manager.menuOpen()).arg(manager.shellModal());
            QTest::mouseMove(&scene.view, start + QPoint(28, 0), 40);
            QTRY_VERIFY2(manager.nativeDragging(), qPrintable("No drag started; the press went to " + pressedOn + sourceInput));
            if (name == "Moved.txt")
            {
                const QDBusReply<bool> stale = finder.call("FinishFileDrag", "wrong-token");
                QVERIFY(stale.isValid());
                QVERIFY(!stale.value());
                QVERIFY(manager.nativeDragging());
            }
            QTest::mouseMove(&scene.view, end, 40);
            if (!between)
                QTRY_VERIFY(manager.nativeDragOnDesktop());
            if (cancel)
            {
                nativeKeyEvent(&scene.view, QEvent::KeyPress, Qt::Key_Escape, {}, 1);
                nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Escape, {}, 1);
            }
            if (name == "Moved.txt")
                QVERIFY(capture(scene.view, "native-folder-drag.png"));
            QTest::mouseRelease(&scene.view, Qt::LeftButton,
                                copy ? Qt::AltModifier : Qt::NoModifier, end);
            if (copy)
                nativeKeyEvent(&scene.view, QEvent::KeyRelease, Qt::Key_Alt, {}, 56);
            QTRY_VERIFY(!manager.nativeDragging());
            QTRY_VERIFY(!scene.compositor.defaultSeat()->pointer()->isButtonPressed());
            if (cancel)
            {
                QVERIFY(QFileInfo::exists(from + '/' + name));
                QVERIFY(!QFileInfo::exists(desktop->directory() + '/' + name));
            }
            else if (conflict)
            {
                QTRY_VERIFY(manager.shellModal());
                QCOMPARE(bytes(from + '/' + name), name.toUtf8());
                QCOMPARE(bytes(desktop->directory() + '/' + name),
                         QByteArray("destination preserved"));
                QTest::keyClick(&scene.view, Qt::Key_Escape);
                QTRY_VERIFY(!manager.shellModal());
            }
            else
            {
                const auto destination = (between ? to : desktop->directory()) + '/' + name;
                QTRY_VERIFY(QFileInfo::exists(destination) || !errors.isEmpty());
                QVERIFY2(
                    errors.isEmpty(),
                    qPrintable(errors.isEmpty() ? QString() : errors.first().first().toString()));
                QVERIFY(QFileInfo::exists(destination));
                QTRY_VERIFY(!desktop->busy());
                QCOMPARE(bytes(destination), name.toUtf8());
                QCOMPARE(QFileInfo::exists(from + '/' + name), copy);
            }
        }
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "desktop-entry-Moved.txt"));
        auto *icon = visualItem(scene.view.contentItem(), "desktop-entry-Moved.txt");
        QTest::mouseClick(&scene.view, Qt::RightButton, {},
                          icon->mapToScene(QPointF(50, 20)).toPoint());
        QTRY_VERIFY(manager.menuOpen());
        auto *duplicate = visualItem(scene.view.contentItem(), "context-duplicate");
        QVERIFY(duplicate);
        duplicate->parentItem()->ensurePolished();
        QVERIFY(capture(scene.view, "native-desktop-context.png"));
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          duplicate->mapToScene(QPointF(60, 12)).toPoint());
        QTRY_VERIFY(QFileInfo::exists(desktop->directory() + "/Moved copy.txt"));
        qInfo("All file drag/menu assertions completed; closing both windows of the shared Finder "
              "client");
        manager.closeWindow(sourceId);
        manager.closeWindow(destinationId);
        QTRY_COMPARE(manager.rowCount(), 0);
    }
    void applicationsBrowserFinderDocumentsAndClientDecorations()
    {
        NativeScene scene;
        WireTrace wire(scene.compositor);
        QElapsedTimer workflowClock;
        workflowClock.start();
        QString phase="start";
        const auto mark=[&](const QString &value) {
            phase=value;
            wire.mark(value);
            qInfo()<<"Application workflow"<<workflowClock.elapsed()<<"ms:"<<value;
        };
        const auto diagnose=qScopeGuard([&]{if(QTest::currentTestFailed()) {
            qWarning()<<"Application workflow failed in"<<phase<<"after"<<workflowClock.elapsed()<<"ms"
                      <<QJsonDocument::fromVariant(scene.manager->snapshot()).toJson(QJsonDocument::Compact);
            wire.print();
            capture(scene.view,"application-workflow-failure.png");
        }});
        QVERIFY(QDir().mkpath(scene.files.path() + "/applications"));
        auto metadata = [&](const QString &label, const QString &name, const QString &extra)
        {
            QFile file(scene.files.path() + "/applications/org.frankenstein.NativeProbe" + label +
                       ".desktop");
            return file.open(QIODevice::WriteOnly) &&
                   file.write(("[Desktop Entry]\nType=Application\nName=" + name +
                               "\nComment=Disposable native qualification application\nExec=\"" +
                               QCoreApplication::applicationDirPath() + "/native-probe\" " + label +
                               extra + "\nMimeType=text/plain;\n")
                                  .toUtf8()) > 0;
        };
        QVERIFY(metadata("Browser", "Catalog Probe", " %f"));
        QVERIFY(metadata("CSD", "Client Decoration Probe", " --frameless"));
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        auto *catalog = qobject_cast<ApplicationCatalog *>(manager.applicationCatalog());
        QVERIFY(catalog);
        auto *browser = scene.view.rootObject()->findChild<QObject *>("application-browser");
        QVERIFY(browser);
        auto *search = scene.view.rootObject()->findChild<QQuickItem *>("application-search");
        QVERIFY(search);
        auto openBrowser = [&]
        {
            // Wait for the Apple menu's command and for the browser, as a person
            // would; the second opening follows a client's teardown and was
            // checked instantly (a sanitizer-speed race, 20261019T092759).
            QTest::mouseClick(&scene.view, Qt::LeftButton, {}, QPoint(25, 13));
            QQuickItem *command = nullptr;
            if (!QTest::qWaitFor([&] {
                    command = visualItem(scene.view.contentItem(), "desktop-command-applications");
                    return command && command->isVisible() && command->isEnabled();
                }, 3000))
                return false;
            command->parentItem()->ensurePolished();
            QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                              command->mapToScene(QPointF(60, 12)).toPoint());
            return QTest::qWaitFor([&] { return browser->property("visible").toBool(); }, 3000);
        };
        mark("open catalog application");
        QVERIFY(openBrowser());
        QTRY_VERIFY(browser->property("opened").toBool());
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(typeAscii(&scene.view, "Catalog"));
        QTRY_VERIFY(catalog->ready());
        QTRY_COMPARE(catalog->count(), 1);
        QTRY_VERIFY(browser->property("acceptEnabled").toBool());
        QVERIFY(manager.shellModal());
        QVERIFY(!scene.compositor.defaultSeat()->keyboardFocus());
        QVERIFY(capture(scene.view, "native-applications.png"));
        QTest::keyClick(&scene.view, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native Probe Browser").isEmpty(), 10000);
        QVERIFY(!manager.shellModal());
        QCOMPARE(manager.activeApplication(), QString("Catalog Probe"));
        QVERIFY(first(manager, "Native Probe Browser").value("serverDecorated").toBool());
        QDBusInterface probe("org.frankenstein.NativeProbeBrowser", "/Probe",
                             "org.frankenstein.NativeProbe");
        QTRY_VERIFY(probe.isValid());
        QCOMPARE(QDBusReply<QString>(probe.call("StyleName")).value(), QString("frankenstein"));
        manager.closeWindow(first(manager, "Native Probe Browser").value("id").toULongLong());
        QTRY_COMPARE(manager.rowCount(), 0);
        // Unmapping the last window is earlier than the process disconnect.
        // Until then the windowless app legitimately still owns the menu bar;
        // its subsequent exit cancels that app's menu tracking. Wait for the
        // owned app to finish before opening the next Finder/Apple menu.
        QTRY_COMPARE(manager.activeApplication(), QString("Finder"));

        mark("open client-decorated application");
        QVERIFY(openBrowser());
        QTRY_VERIFY(browser->property("opened").toBool());
        QTRY_VERIFY(search->hasActiveFocus());
        QVERIFY(typeAscii(&scene.view, "Decoration"));
        QTRY_COMPARE(catalog->count(), 1);
        QTRY_VERIFY(browser->property("acceptEnabled").toBool());
        QTest::keyClick(&scene.view, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native Probe CSD").isEmpty(), 10000);
        const auto csd = first(manager, "Native Probe CSD");
        QVERIFY(!csd.value("serverDecorated").toBool());
        QVERIFY(!csd.value("hasCloseBox").toBool());
        const auto csdId = csd.value("id").toULongLong();
        QVERIFY(!manager.policyState().windows.at({csdId}).shade_eligible);
        auto *frame = visualItem(scene.view.contentItem(), "frame-" + QString::number(csdId));
        QVERIFY(frame);
        QCOMPARE(frame->width(), qreal(csd.value("width").toInt()));
        QCOMPARE(frame->height(), qreal(csd.value("height").toInt()));
        manager.closeWindow(csdId);
        QTRY_COMPARE(manager.rowCount(), 0);
        QTRY_COMPARE(manager.activeApplication(), QString("Finder"));

        const auto documents = scene.files.path() + "/Documents";
        QVERIFY(QDir().mkpath(documents));
        const auto document = documents + "/Finder document.txt";
        {
            QFile file(document);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("Opened through Finder and the desktop.");
        }
        // A different executable on the same private bus cannot impersonate Finder.
        auto request = QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                      "org.frankenstein.Desktop", "OpenDocument");
        request << document << false;
        QDBusPendingCallWatcher denied(QDBusConnection::sessionBus().asyncCall(request));
        QTRY_VERIFY(denied.isFinished());
        const QDBusPendingReply<bool> denial = denied;
        QVERIFY(!denial.isError());
        QVERIFY(!denial.value());
        mark("launch Finder document window");
        manager.launchFinder(documents);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Documents").isEmpty(), 10000);
        const auto finderId = first(manager, "Documents").value("id").toULongLong();
        const auto finderService =
            "org.frankenstein.Finder.p" +
            QString::fromLatin1(
                QCryptographicHash::hash(
                    (scene.files.path() + "\n" + scene.files.path() + "/profile").toUtf8(),
                    QCryptographicHash::Sha256)
                    .toHex()
                    .left(24));
        QDBusInterface finder(finderService, "/Finder", "org.frankenstein.Finder");
        QTRY_VERIFY(finder.isValid());
        QTRY_VERIFY(QDBusReply<QVariantMap>(finder.call("DirectoryState", documents))
                        .value()
                        .value("loaded")
                        .toBool());
        // Loading the directory does not mean Qt has processed window activation
        // yet. Shortcut dispatch depends on the client-side active window too.
        QTRY_VERIFY(QDBusReply<QVariantMap>(finder.call("DirectoryState", documents))
                        .value().value("windowActive").toBool());
        auto *finderClient=qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(),"client-"+QString::number(finderId)));
        QVERIFY(finderClient && finderClient->surface());
        QTRY_COMPARE(scene.compositor.defaultSeat()->keyboardFocus(),finderClient->surface());
        mark("Finder activated; select document");
        commandKey(&scene.view, Qt::Key_A, 30);
        QTRY_COMPARE(QDBusReply<QVariantMap>(finder.call("DirectoryState", documents))
                         .value().value("selectedItems").toInt(),1);
        mark("document selected; open through Finder keyboard command");
        commandKey(&scene.view, Qt::Key_O, 24);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Finder document.txt").isEmpty(), 10000);
        mark("document opened; close editor");
        const auto documentApplication = manager.activeApplication();
        manager.closeWindow(first(manager, "Finder document.txt").value("id").toULongLong());
        QTRY_VERIFY(first(manager, "Finder document.txt").isEmpty());
        // The editor stays active without windows until it quits. A menu opened before then is
        // tracked in its context, and correctly closes when that application exits; wait for it.
        const auto running = [&](const QString &name) {
            for (const auto &entry : manager.applications())
                if (entry.toMap().value("name").toString() == name)
                    return true;
            return false;
        };
        if (documentApplication != "Finder")
            QTRY_VERIFY_WITH_TIMEOUT(!running(documentApplication), 10000);
        // The Control Strip's Applications module lists Finder below its panel command.
        mark("return to Finder through Control Strip");
        auto *applicationsModule = visualItem(scene.view.contentItem(), "control-strip-applications");
        QVERIFY(applicationsModule);
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          applicationsModule->mapToScene(QPointF(16, 14)).toPoint());
        QQuickItem *finderRow = nullptr;
        QTRY_VERIFY((finderRow = visualItem(scene.view.contentItem(), "desktop-command-finder")) &&
                    finderRow->isVisible());
        finderRow->parentItem()->ensurePolished();
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          finderRow->mapToScene(QPointF(40, finderRow->height() / 2)).toPoint());
        QTRY_COMPARE(manager.activeApplication(), QString("Finder"));
        QTRY_VERIFY(!manager.menuOpen());
        auto *broker = qobject_cast<MenuBroker *>(manager.menuBroker());
        QVERIFY(broker);
        QTRY_VERIFY(!broker->busy() && command(broker->menus(), "Open With…") >= 0);
        broker->invoke(command(broker->menus(), "Open With…"), broker->generation(),
                       broker->revision());
        QTRY_VERIFY(browser->property("opened").toBool());
        QCOMPARE(browser->property("documentPath").toString(), document);
        QTRY_VERIFY(search->hasActiveFocus());
        mark("open document with chosen application");
        QVERIFY(typeAscii(&scene.view, "Catalog"));
        QTRY_COMPARE(catalog->count(), 1);
        auto *remember = visualItem(scene.view.contentItem(), "application-remember");
        QVERIFY(remember);
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          remember->mapToScene(QPointF(10, remember->height() / 2)).toPoint());
        QVERIFY(remember->property("checked").toBool());
        QVERIFY(capture(scene.view, "native-open-with.png"));
        auto *open = visualItem(scene.view.contentItem(), "application-browser-accept");
        QVERIFY(open);
        QTest::mouseClick(
            &scene.view, Qt::LeftButton, {},
            open->mapToScene(QPointF(open->width() / 2, open->height() / 2)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native Probe Browser").isEmpty(), 10000);
        QDBusInterface reopened("org.frankenstein.NativeProbeBrowser", "/Probe",
                                "org.frankenstein.NativeProbe");
        QTRY_VERIFY(reopened.isValid());
        QVERIFY(QDBusReply<QStringList>(reopened.call("Arguments")).value().contains(document));
        QTRY_VERIFY(QFileInfo(scene.files.path() + "/profile/associations.json").isFile());
        QCOMPARE(catalog->preferredApplication(document),
                 QString("org.frankenstein.NativeProbeBrowser.desktop"));
        QCOMPARE(bytes(document), QByteArray("Opened through Finder and the desktop."));
        manager.closeWindow(first(manager, "Native Probe Browser").value("id").toULongLong());
        manager.closeWindow(finderId);
        QTRY_COMPARE(manager.rowCount(), 0);
    }
    void desktopFileOperationsAndShellDialogsProtectDocuments()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        const auto guarded = scene.files.path() + "/Guarded.txt";
        const auto source = items->directory() + "/Copy source.txt";
        for (const auto &path : {guarded, source})
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("protected contents");
        }
        QTRY_COMPARE(items->rowCount(), 3);
        QProcess editor;
        editor.setProcessEnvironment(scene.childEnvironment());
        editor.start(QCoreApplication::applicationDirPath() + "/frankenstein-editor", {guarded});
        QVERIFY(editor.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Guarded.txt").isEmpty(), 10000);
        const auto editorId = first(manager, "Guarded.txt").value("id").toULongLong();
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, QPoint(35, 690));
        QVERIFY(manager.desktopActive());
        QVERIFY(clickDesktopCommand(scene, "new-folder"));
        auto *dialog = scene.view.rootObject()->findChild<QObject *>("desktop-name-dialog");
        auto *message = scene.view.rootObject()->findChild<QObject *>("desktop-message-dialog");
        auto *field = scene.view.rootObject()->findChild<QQuickItem *>("desktop-name-field");
        QVERIFY(dialog && message && field);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(field->hasActiveFocus());
        QVERIFY(manager.shellModal());
        QVERIFY(!scene.compositor.defaultSeat()->keyboardFocus());
        commandKey(&scene.view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&scene.view, "Cancelled folder"));
        QCOMPARE(field->property("text").toString(), QString("Cancelled folder"));
        QTest::keyClick(&scene.view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(!QFileInfo(items->directory() + "/Cancelled folder").exists());
        QVERIFY(clickDesktopCommand(scene, "new-folder"));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTRY_VERIFY(field->hasActiveFocus());
        commandKey(&scene.view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&scene.view, "Native folder"));
        QCOMPARE(field->property("text").toString(), QString("Native folder"));
        QVERIFY(capture(scene.view, "native-desktop-create.png"));
        QSignalSpy accepted(dialog, SIGNAL(accepted()));
        QTest::keyClick(&scene.view, Qt::Key_Return);
        QTRY_COMPARE(accepted.count(), 1);
        QTRY_VERIFY(!dialog->property("visible").toBool());
        QTRY_VERIFY(!items->busy() && QFileInfo(items->directory() + "/Native folder").isDir());
        QTRY_VERIFY(!manager.shellModal());
        QTest::keyClick(&scene.view, Qt::Key_F2);
        QTRY_VERIFY(dialog->property("opened").toBool());
        commandKey(&scene.view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&scene.view, "Renamed folder"));
        QCOMPARE(field->property("text").toString(), QString("Renamed folder"));
        QVERIFY(capture(scene.view, "native-desktop-naming.png"));
        QTest::keyClick(&scene.view, Qt::Key_Return);
        const auto renamed = items->directory() + "/Renamed folder";
        QTRY_VERIFY(!items->busy() && QFileInfo(renamed).isDir());
        QTRY_VERIFY(!manager.shellModal());
        QVERIFY(!QFileInfo(items->directory() + "/Native folder").exists());
        auto *sourceIcon = visualItem(scene.view.contentItem(), "desktop-entry-Copy source.txt");
        auto *folderIcon = visualItem(scene.view.contentItem(), "desktop-entry-Renamed folder");
        QVERIFY(sourceIcon && folderIcon);
        const auto start = sourceIcon->mapToScene(QPointF(50, 20)).toPoint();
        const auto finish = folderIcon->mapToScene(QPointF(50, 20)).toPoint();
        QTest::mousePress(&scene.view, Qt::LeftButton, {}, start);
        QTest::mouseMove(&scene.view, finish, 40);
        QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, finish);
        QTRY_COMPARE(bytes(renamed + "/Copy source.txt"), QByteArray("protected contents"));
        QTRY_VERIFY(!items->busy());
        QVERIFY(!QFileInfo(source).exists());
        folderIcon = visualItem(scene.view.contentItem(), "desktop-entry-Renamed folder");
        QVERIFY(folderIcon);
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          folderIcon->mapToScene(QPointF(50, 20)).toPoint());
        commandKey(&scene.view, Qt::Key_D, 32);
        const auto copy = items->directory() + "/Renamed folder copy";
        QTRY_COMPARE(bytes(copy + "/Copy source.txt"), QByteArray("protected contents"));
        QTRY_VERIFY(!items->busy());
        QSignalSpy completed(items, &DesktopItems::operationFinished);
        commandKey(&scene.view, Qt::Key_Backspace, 14);
        QTRY_VERIFY(!items->busy() && !QFileInfo(copy).exists());
        QVERIFY(!completed.isEmpty());
        const auto trashed = completed.last()[1].toStringList().first();
        QCOMPARE(bytes(trashed + "/Copy source.txt"), QByteArray("protected contents"));
        QVERIFY(QFileInfo(scene.files.path() + "/profile/data/Trash/info/" +
                          QFileInfo(trashed).fileName() + ".trashinfo")
                    .isFile());
        auto *trashIcon = visualItem(scene.view.contentItem(), "desktop-entry-Trash");
        QVERIFY(trashIcon);
        QTest::mouseDClick(&scene.view, Qt::LeftButton, {},
                           trashIcon->mapToScene(QPointF(50, 20)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Trash").isEmpty(), 10000);
        const auto trashId = first(manager, "Trash").value("id").toULongLong();
        const auto profile = scene.files.path() + "/profile";
        const auto service = "org.frankenstein.Finder.p" +
                             QString::fromLatin1(QCryptographicHash::hash(
                                                     (scene.files.path() + "\n" + profile).toUtf8(),
                                                     QCryptographicHash::Sha256)
                                                     .toHex()
                                                     .left(24));
        QDBusInterface finder(service, "/Finder", "org.frankenstein.Finder");
        QVERIFY(finder.isValid());
        const auto trashDirectory = profile + "/data/Trash/files";
        auto folderState = [&]
        { return QDBusReply<QVariantMap>(finder.call("DirectoryState", trashDirectory)).value(); };
        QTRY_VERIFY(folderState().value("loaded").toBool());
        QCOMPARE(folderState().value("items").toInt(), 1);
        auto *trashClient = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(trashId)));
        QVERIFY(trashClient);
        QTRY_VERIFY(trashClient->hasActiveFocus());
        QTRY_VERIFY(folderState().value("windowActive").toBool());
        // Restore through the real Finder's Select All / Put Away shortcuts.
        commandKey(&scene.view, Qt::Key_A, 30);
        QTRY_COMPARE(folderState().value("selectedItems").toInt(), 1);
        commandKey(&scene.view, Qt::Key_Y, 21);
        QTRY_COMPARE(bytes(copy + "/Copy source.txt"), QByteArray("protected contents"));
        QTRY_VERIFY(!folderState().value("busy").toBool());
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "desktop-entry-Renamed folder copy"));
        auto *copyIcon = visualItem(scene.view.contentItem(), "desktop-entry-Renamed folder copy");
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          copyIcon->mapToScene(QPointF(50, 20)).toPoint());
        QVERIFY(manager.desktopActive());
        QTest::keyClick(&scene.view, Qt::Key_F2);
        QTRY_VERIFY(dialog->property("opened").toBool());
        commandKey(&scene.view, Qt::Key_A, 30);
        QVERIFY(typeAscii(&scene.view, "Renamed folder"));
        QCOMPARE(field->property("text").toString(), QString("Renamed folder"));
        QTest::keyClick(&scene.view, Qt::Key_Return);
        QTRY_VERIFY(message->property("opened").toBool());
        QVERIFY(manager.shellModal());
        QVERIFY(!scene.compositor.defaultSeat()->keyboardFocus());
        QVERIFY(capture(scene.view, "native-desktop-conflict.png"));
        QTest::keyClick(&scene.view, Qt::Key_Escape);
        QTRY_VERIFY(!manager.shellModal());
        QCOMPARE(bytes(copy + "/Copy source.txt"), QByteArray("protected contents"));
        QCOMPARE(bytes(renamed + "/Copy source.txt"), QByteArray("protected contents"));
        // Qt may replace a native toplevel while this long workflow is in the
        // Finder. Close the editor's current owned surface, as its frame does,
        // rather than an ID captured before the intervening dialog workflows.
        QVariantMap currentEditor;
        QTRY_VERIFY(!(currentEditor=first(manager,"Guarded.txt")).isEmpty());
        const auto currentEditorId=currentEditor.value("id").toULongLong();
        auto *currentClient=qobject_cast<QWaylandQuickItem *>(visualItem(scene.view.contentItem(),"client-"+QString::number(currentEditorId)));
        QVERIFY(currentClient);
        QCOMPARE(liveSurfaceClientPid(currentClient->surface()),editor.processId());
        if(currentEditorId!=editorId)
            qInfo()<<"Editor native surface replaced during Finder workflow:"<<editorId<<"to"<<currentEditorId;
        manager.closeWindow(currentEditorId);
        QTRY_COMPARE(editor.state(), QProcess::NotRunning);
        QVERIFY(first(manager, "Save Changes?").isEmpty());
        QCOMPARE(bytes(guarded), QByteArray("protected contents"));
        manager.closeWindow(trashId);
        QTRY_COMPARE(manager.rowCount(), 0);
        QVERIFY(manager.desktopActive());
    }
    void nativeApplicationPolicyAndMenuCommand()
    {
        QVERIFY2(QDBusConnection::sessionBus().isConnected(),
                 "Run scripts/qualify-native.sh: a private session bus is required.");
        QTemporaryDir fixture;
        QWaylandQuickCompositor compositor;
        QWaylandXdgShell shell(&compositor);
        QWaylandXdgDecorationManagerV1 decoration;
        std::unique_ptr<DesktopOutputGeometry> outputGeometry;
        std::unique_ptr<NativeMenuBridge> bridge;
        DesktopClientRetirement retireClients{&compositor};
        QQuickView view;
        view.resize(1120, 760);
        compositor.setSocketName("frankenstein-qualification");
        decoration.setExtensionContainer(&compositor);
        decoration.setPreferredMode(QWaylandXdgToplevel::ServerSideDecoration);
        compositor.create();
        shell.initialize();
        decoration.initialize();
        outputGeometry = std::make_unique<DesktopOutputGeometry>(&compositor, &view);
        bridge = std::make_unique<NativeMenuBridge>(&compositor);
        NativeManager manager(&compositor, &view, fixture.path(), fixture.path() + "/profile", {});
        manager.attachShell(&shell);
        const auto clearView = qScopeGuard([&] { releaseDesktopView(view); });
        const auto failureEvidence = qScopeGuard(
            [&]
            {
                if (QTest::currentTestFailed() && view.rootObject())
                {
                    capture(view, "native-policy-menu-failure.png");
                    qInfo().noquote() << "Policy failure state:"
                                      << QJsonDocument::fromVariant(manager.snapshot())
                                             .toJson(QJsonDocument::Compact)
                                      << "openMenu=" << view.rootObject()->property("openMenu");
                }
            });
        connect(bridge.get(), &NativeMenuBridge::endpointChanged, &manager,
                &NativeManager::setMenuEndpoint);
        view.rootContext()->setContextProperty("manager", &manager);
        view.setSource(QUrl("qrc:/desktop/src/desktop/Desktop.qml"));
        QVERIFY(view.status() != QQuickView::Error);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        qInfo("Nested display: %.2fx DPR; logical size %dx%d", view.devicePixelRatio(),
              view.width(), view.height());
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("WAYLAND_DISPLAY", QString::fromUtf8(compositor.socketName()));
        environment.insert("QT_QPA_PLATFORM", "wayland");
        environment.insert("QT_QPA_PLATFORMTHEME", "kde");
        environment.insert("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
        environment.insert("QT_WAYLAND_CLIENT_BUFFER_INTEGRATION", "shm");
        environment.insert("XDG_CONFIG_HOME", fixture.path() + "/config");
        environment.insert("XDG_DATA_HOME", fixture.path() + "/data");
        environment.insert("XDG_CACHE_HOME", fixture.path() + "/cache");
        environment.remove("DISPLAY");
        environment.remove("XAUTHORITY");
        environment.remove("WAYLAND_SOCKET");
        QProcess a, b;
        a.setProcessEnvironment(environment);
        b.setProcessEnvironment(environment);
        a.start(QCoreApplication::applicationDirPath() + "/native-probe", {"A"});
        QVERIFY(a.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native Probe A").isEmpty(), 10000);
        auto wa = first(manager, "Native Probe A");
        const auto aid = wa.value("id").toULongLong();
        auto *client = qobject_cast<QWaylandQuickItem *>(
            visualItem(view.contentItem(), "client-" + QString::number(aid)));
        auto *frame = visualItem(view.contentItem(), "frame-" + QString::number(aid));
        QVERIFY(client);
        QVERIFY(frame);
        QTRY_VERIFY(client->surface());
        const auto expected = QSizeF(client->surface()->destinationSize());
        QTRY_VERIFY(qAbs(client->mapRectToItem(frame, client->boundingRect()).width() -
                         expected.width()) < 0.01);
        QTRY_VERIFY(qAbs(client->mapRectToItem(frame, client->boundingRect()).height() -
                         expected.height()) < 0.01);
        QTRY_VERIFY(qAbs(client->mapRectToItem(frame, client->boundingRect()).width() -
                         (frame->width() - 6)) < 0.01);
        QTRY_VERIFY(qAbs(client->mapRectToItem(frame, client->boundingRect()).height() -
                         (frame->height() - 27)) < 0.01);
        QDBusInterface apiA("org.frankenstein.NativeProbeA", "/Probe",
                            "org.frankenstein.NativeProbe");
        QVERIFY(apiA.isValid());
        const QSize before(wa.value("width").toInt(), wa.value("height").toInt());
        auto *appearance = qobject_cast<AppearanceSettings *>(manager.appearance());
        QVERIFY(appearance && appearance->apply(600, true));
        manager.shade(aid);
        QVERIFY(first(manager, "Native Probe A").value("collapsed").toBool());
        QTRY_VERIFY(first(manager, "Native Probe A").value("shadeAnimating").toBool());
        QTRY_VERIFY(frame->height() > 23 && frame->height() < before.height() + 27);
        QVERIFY(frame->clip());
        QVERIFY(!compositor.defaultSeat()->keyboardFocus());
        QTest::keyClick(&view, Qt::Key_Z);
        QCOMPARE(QDBusReply<QString>(apiA.call("Text")).value(), QString());
        QCOMPARE(client->surface()->destinationSize(), before);
        QVERIFY(capture(view, "native-windowshade-midpoint.png"));
        manager.moveWindow(aid, 180, 100);
        QTRY_VERIFY(!first(manager, "Native Probe A").value("shadeAnimating").toBool());
        QCOMPARE(frame->height(), qreal(23));
        QVERIFY(!client->isVisible());
        manager.shade(aid);
        QTRY_VERIFY(first(manager, "Native Probe A").value("shadeAnimating").toBool());
        QVERIFY(!compositor.defaultSeat()->keyboardFocus());
        QTest::keyClick(&view, Qt::Key_Z);
        QTRY_VERIFY(!first(manager, "Native Probe A").value("shadeAnimating").toBool());
        QTRY_COMPARE(compositor.defaultSeat()->keyboardFocus(), client->surface());
        QCOMPARE(QDBusReply<QString>(apiA.call("Text")).value(), QString());
        QCOMPARE(client->surface()->destinationSize(), before);
        QCOMPARE(frame->height(), qreal(before.height() + 27));
        QVERIFY(!frame->clip());
        wa = first(manager, "Native Probe A");
        QCOMPARE(QSize(wa.value("width").toInt(), wa.value("height").toInt()), before);
        QCOMPARE(wa.value("x").toInt(), 180);
        manager.hideApplication();
        QVERIFY(!first(manager, "Native Probe A").value("visible").toBool());
        manager.activate(aid);
        QVERIFY(first(manager, "Native Probe A").value("visible").toBool());
        b.start(QCoreApplication::applicationDirPath() + "/native-probe", {"B"});
        QVERIFY(b.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native Probe B").isEmpty(), 10000);
        auto wb = first(manager, "Native Probe B");
        const auto bid = wb.value("id").toULongLong();
        manager.moveWindow(bid, 650, 300);
        QTest::qWait(100);
        // The first body click activates A and is consumed. Subsequent typing
        // must reach A's real Wayland client, not the shell or B.
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(220, 160));
        QTRY_VERIFY(first(manager, "Native Probe A").value("active").toBool());
        QTest::qWait(30);
        QCOMPARE(QDBusReply<int>(apiA.call("MousePresses")).value(), 0);
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(220, 160));
        QTRY_COMPARE(QDBusReply<int>(apiA.call("MousePresses")).value(), 1);
        // Reach the far edge of the content, which lay outside the shrunken
        // surface on the original 1.5x capture. Input and visual units must agree.
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(513, 343));
        QTRY_COMPARE(QDBusReply<int>(apiA.call("MousePresses")).value(), 2);
        QVERIFY(qAbs(QDBusReply<int>(apiA.call("LastX")).value() - 330) <= 3);
        QVERIFY(qAbs(QDBusReply<int>(apiA.call("LastY")).value() - 220) <= 3);
        for (char key : QByteArray("native input"))
            QTest::keyClick(&view, key);
        QTRY_COMPARE(QDBusReply<QString>(apiA.call("Text")).value(), QString("native input"));
        auto *broker = qobject_cast<MenuBroker *>(manager.menuBroker());
        QVERIFY(broker);
        QTRY_VERIFY_WITH_TIMEOUT(
            !broker->busy() && command(broker->menus(), "Increment Probe") >= 0, 5000);
        const int action = command(broker->menus(), "Increment Probe");
        // Exercise the actual exported menu UI, not just its broker method.
        auto *bar = visualItem(view.contentItem(), "bar-entry-1");
        QVERIFY(bar);
        // Exported roots can replace the shell's fallback menu row. Its Row
        // positions are deferred until polish, even when labels already exist.
        bar->parentItem()->ensurePolished();
        QTRY_VERIFY(bar->isVisible() && bar->isEnabled() && bar->width() > 0);
        QTest::mouseClick(&view, Qt::LeftButton, {},
                          bar->mapToScene(QPointF(bar->width() / 2, 13)).toPoint());
        QTRY_VERIFY(manager.menuOpen());
        const auto itemName = "menu-command-" + QString::number(action);
        QTRY_VERIFY(!broker->busy());
        QTRY_VERIFY(view.rootObject()->property("exportPopup").value<QObject *>() &&
                    view.rootObject()
                        ->property("exportPopup")
                        .value<QObject *>()
                        ->property("opened")
                        .toBool());
        QPointer<QQuickItem> item;
        QTRY_VERIFY((item = visualItem(view.contentItem(), itemName)) && item->isVisible() &&
                    item->isEnabled() && item->width() > 0 &&
                    item->mapToScene(QPointF(0, 0)).y() >= 26);
        item->parentItem()->ensurePolished();
        QTest::mouseClick(
            &view, Qt::LeftButton, {},
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTRY_COMPARE(QDBusReply<int>(apiA.call("Counter")).value(), 1);
        const auto oldGeneration = broker->generation();
        const auto oldRevision = broker->revision();
        manager.activate(bid);
        broker->invoke(action, oldGeneration, oldRevision);
        QTest::qWait(100);
        QCOMPARE(QDBusReply<int>(apiA.call("Counter")).value(), 1);
        const auto evidence = qEnvironmentVariable(
            "FRANKENSTEIN_EVIDENCE_DIR", QCoreApplication::applicationDirPath() + "/artifacts");
        QDir().mkpath(evidence);
        QVERIFY(view.grabWindow().save(evidence + "/native-observed.png"));
        apiA.call("VetoClose", true);
        manager.closeWindow(aid);
        QTest::qWait(100);
        QVERIFY(!first(manager, "Native Probe A").isEmpty());
        apiA.call("Finish");
        QTRY_VERIFY(first(manager, "Native Probe A").isEmpty());
        QTRY_COMPARE(a.state(), QProcess::NotRunning);
        QDBusInterface apiB("org.frankenstein.NativeProbeB", "/Probe",
                            "org.frankenstein.NativeProbe");
        apiB.call("Finish");
        QTRY_COMPARE(b.state(), QProcess::NotRunning);
        QTRY_COMPARE(manager.rowCount(), 0);
        QTRY_COMPARE(manager.applications().size(), 1);
        QCOMPARE(manager.applications().first().toMap().value("name").toString(),
                 QString("Finder"));
        QVERIFY(manager.desktopActive());
        view.setSource({});
    }
    void realFinderAndEditorWorkflow()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        const auto root = scene.files.path() + "/Macintosh HD",
                   profile = scene.files.path() + "/finder-profile";
        QVERIFY(QDir().mkpath(root + "/Documents"));
        QProcess finder;
        finder.setProcessEnvironment(scene.childEnvironment());
        const auto finderProgram = QCoreApplication::applicationDirPath() + "/frankenstein-finder";
        finder.start(finderProgram, {"--root", root, "--profile", profile});
        QVERIFY(finder.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Hard Disk").isEmpty(), 10000);
        const auto rootId = first(manager, "Hard Disk").value("id").toULongLong();
        // A second real Finder process forwards to the existing folder owner.
        QProcess request;
        request.setProcessEnvironment(scene.childEnvironment());
        const QStringList arguments{"--root", root,     "--profile",
                                    profile,  "--open", root + "/Documents"};
        request.start(finderProgram, arguments);
        QVERIFY(request.waitForStarted());
        QTRY_COMPARE(request.state(), QProcess::NotRunning);
        QCOMPARE(request.exitCode(), 0);
        QTRY_COMPARE(manager.rowCount(), 2);
        QVERIFY(!first(manager, "Documents").isEmpty());
        request.start(finderProgram, arguments);
        QVERIFY(request.waitForStarted());
        QTRY_COMPARE(request.state(), QProcess::NotRunning);
        QCOMPARE(request.exitCode(), 0);
        QCOMPARE(manager.rowCount(), 2);
        // Persist an actual compositor-configured content size through Finder exit.
        QElapsedTimer resizeTimer;
        resizeTimer.start();
        manager.resizeWindow(rootId, 700, 390);
        QTRY_COMPARE(first(manager, "Hard Disk").value("width").toInt(), 700);
        QTRY_COMPARE(first(manager, "Hard Disk").value("height").toInt(), 390);
        qInfo("Background configure reached 700x390 in %lld ms", resizeTimer.elapsed());
        manager.closeWindow(rootId);
        QTRY_VERIFY(first(manager, "Hard Disk").isEmpty());
        manager.closeWindow(first(manager, "Documents").value("id").toULongLong());
        QTRY_COMPARE(finder.state(), QProcess::NotRunning);
        QTRY_COMPARE(manager.rowCount(), 0);
        finder.start(finderProgram, {"--root", root, "--profile", profile});
        QVERIFY(finder.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Hard Disk").isEmpty(), 10000);
        QTRY_COMPARE(first(manager, "Hard Disk").value("width").toInt(), 700);
        QTRY_COMPARE(first(manager, "Hard Disk").value("height").toInt(), 390);
        const auto document = root + "/Native document.txt";
        {
            QFile file(document);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("before"), 6);
        }
        QProcess editor;
        editor.setProcessEnvironment(scene.childEnvironment());
        editor.start(QCoreApplication::applicationDirPath() + "/frankenstein-editor", {document});
        QVERIFY(editor.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Native document.txt").isEmpty(), 10000);
        const auto editorWindow = first(manager, "Native document.txt");
        const auto eid = editorWindow.value("id").toULongLong();
        QTest::mouseClick(
            &scene.view, Qt::LeftButton, {},
            QPoint(editorWindow.value("x").toInt() + 30, editorWindow.value("y").toInt() + 55));
        commandKey(&scene.view, Qt::Key_A, 30);
        for (char key : QByteArray("saved through the native desktop"))
            QTest::keyClick(&scene.view, key);
        commandKey(&scene.view, Qt::Key_S, 31);
        QTRY_COMPARE(bytes(document), QByteArray("saved through the native desktop"));
        for (char key : QByteArray(" unsaved change"))
            QTest::keyClick(&scene.view, key);
        auto *broker = qobject_cast<MenuBroker *>(manager.menuBroker());
        QVERIFY(broker);
        QTRY_VERIFY(command(broker->menus(), "Save") >= 0);
        const auto saveCommand = command(broker->menus(), "Save");
        const auto previousGeneration = broker->generation();
        const auto previousRevision = broker->revision();
        altKey(&scene.view, Qt::Key_Q, 16);
        QTRY_VERIFY(!first(manager, "Save Changes?").isEmpty());
        const auto dialog = first(manager, "Save Changes?").value("id").toULongLong();
        QVERIFY(manager.policyState().windows.at({dialog}).modal);
        QVERIFY(!manager.policyState().windows.at({dialog}).shade_eligible);
        const auto alert = first(manager, "Save Changes?");
        QVERIFY(!alert.value("canResize").toBool());
        QVERIFY(!alert.value("canZoom").toBool());
        QVERIFY(!alert.value("hasCloseBox").toBool());
        altKey(&scene.view, Qt::Key_Q, 16); // Leave the application's save decision open.
        QCOMPARE(first(manager, "Save Changes?").value("id").toULongLong(), dialog);
        for (const auto &kind : {"close-box-", "zoom-box-", "shade-box-", "resize-box-"})
        {
            auto *control = visualItem(scene.view.contentItem(), kind + QString::number(dialog));
            QVERIFY(control);
            QVERIFY(!control->isVisible());
        }
        manager.resizeWindow(dialog, 800, 600);
        manager.zoom(dialog);
        QCOMPARE(first(manager, "Save Changes?").value("width"), alert.value("width"));
        QCOMPARE(first(manager, "Save Changes?").value("height"), alert.value("height"));
        QTRY_VERIFY(broker->interactionBlocked());
        QCOMPARE(broker->generation(), previousGeneration);
        QCOMPARE(command(broker->menus(), "Save"), saveCommand);
        auto entryValue = scene.view.rootObject()->property("barEntries");
        if (entryValue.metaType() == QMetaType::fromType<QJSValue>())
            entryValue = entryValue.value<QJSValue>().toVariant();
        const auto entries = entryValue.toList();
        QVERIFY(entries.size() > 1);
        QCOMPARE(entries[1].toMap().value("label").toString(), QString("File"));
        QVERIFY(!entries[1].toMap().value("enabled").toBool());
        auto *disabledFile = visualItem(scene.view.contentItem(), "bar-entry-1");
        QVERIFY(disabledFile);
        disabledFile->parentItem()->ensurePolished();
        QTest::mouseClick(&scene.view, Qt::LeftButton, {},
                          disabledFile->mapToScene(QPointF(20, 13)).toPoint());
        QVERIFY(manager.policyState().active.window.has_value());
        QCOMPARE(manager.policyState().active.window->value, dialog);
        QVERIFY(!manager.menuOpen());
        // Both a current action and one captured before the alert must be refused.
        broker->invoke(saveCommand, broker->generation(), broker->revision());
        broker->invoke(saveCommand, previousGeneration, previousRevision);
        QTest::qWait(100);
        QCOMPARE(bytes(document), QByteArray("saved through the native desktop"));
        QVERIFY(!first(manager, "Save Changes?").isEmpty());
        QVERIFY(capture(scene.view, "native-save-dialog.png"));
        manager.activate(eid);
        QCOMPARE(manager.policyState().active.window->value, dialog);
        QTest::keyClick(&scene.view, Qt::Key_Escape);
        QTRY_VERIFY(first(manager, "Save Changes?").isEmpty());
        QVERIFY(manager.policyState().windows.contains({eid}));
        QTRY_VERIFY(!broker->interactionBlocked());
        broker->invoke(saveCommand, previousGeneration, previousRevision);
        QTest::qWait(100);
        QCOMPARE(bytes(document), QByteArray("saved through the native desktop"));
        auto *editorClient = qobject_cast<QWaylandQuickItem *>(
            visualItem(scene.view.contentItem(), "client-" + QString::number(eid)));
        QVERIFY(editorClient);
        QTRY_VERIFY(editorClient->hasActiveFocus());
        QCOMPARE(scene.compositor.defaultSeat()->keyboardFocus(), editorClient->surface());
        commandKey(&scene.view, Qt::Key_S, 31);
        QTRY_COMPARE(bytes(document),
                     QByteArray("saved through the native desktop unsaved change"));
        manager.closeWindow(eid);
        QTRY_COMPARE(editor.state(), QProcess::NotRunning);
        manager.closeWindow(first(manager, "Hard Disk").value("id").toULongLong());
        QTRY_COMPARE(finder.state(), QProcess::NotRunning);
        QTRY_COMPARE(manager.rowCount(), 0);
    }
    void rendererTeardownWithConnectedClientKeepsDisplayAlive()
    {
        QProcess probe;
        const auto cleanupProbe = qScopeGuard(
            [&]
            {
                if (probe.state() != QProcess::NotRunning)
                {
                    probe.kill(); // Only this disposable, test-owned probe.
                    probe.waitForFinished(5000);
                }
            });
        {
            NativeScene scene;
            QVERIFY(scene.show());
            probe.setProcessEnvironment(scene.childEnvironment());
            probe.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Teardown"});
            QVERIFY(probe.waitForStarted());
            QTRY_VERIFY_WITH_TIMEOUT(!first(*scene.manager, "Native Probe Teardown").isEmpty(),
                                     10000);
            // Finish a rendered frame while the client is still connected.
            QVERIFY(capture(scene.view, "native-before-teardown.png"));
        }
        // Closing this display intentionally disconnects its disposable client.
        // The compositor must survive buffer release during renderer shutdown.
        QTRY_COMPARE(probe.state(), QProcess::NotRunning);
        {
            NativeScene scene;
            QVERIFY(scene.show());
            probe.setProcessEnvironment(scene.childEnvironment());
            probe.start(QCoreApplication::applicationDirPath() + "/native-probe", {"Abrupt"});
            QVERIFY(probe.waitForStarted());
            QTRY_VERIFY_WITH_TIMEOUT(!first(*scene.manager, "Native Probe Abrupt").isEmpty(),
                                     10000);
            QVERIFY(!scene.view.grabWindow().isNull());
            probe.kill(); // Deliberate failure injection for this disposable probe only.
            QTRY_COMPARE(probe.state(), QProcess::NotRunning);
            QTRY_COMPARE(scene.manager->rowCount(), 0);
            QVERIFY(capture(scene.view, "native-after-client-exit.png"));
        }
    }
    void desktopFilesLaunchNativeApplicationsAndKeepKeyboardFocus()
    {
        NativeScene scene;
        QVERIFY(scene.show());
        auto &manager = *scene.manager;
        auto *items = qobject_cast<DesktopItems *>(manager.desktopItems());
        QVERIFY(items);
        const auto note = items->directory() + "/Desktop note.txt";
        {
            QFile file(note);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("Opened from the native desktop."), 31);
        }
        QVERIFY(QDir(items->directory()).mkdir("Desktop folder"));
        QTRY_COMPARE(items->rowCount(), 4);
        QTRY_VERIFY(visualItem(scene.view.contentItem(), "desktop-entry-Desktop folder"));
        auto *folder = visualItem(scene.view.contentItem(), "desktop-entry-Desktop folder");
        QTest::mouseDClick(&scene.view, Qt::LeftButton, {},
                           folder->mapToScene(QPointF(50, 20)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Desktop folder").isEmpty(), 10000);
        const auto finderId = first(manager, "Desktop folder").value("id").toULongLong();
        QVERIFY(!manager.desktopActive());
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, QPoint(35, 690));
        QVERIFY(manager.desktopActive());
        QVERIFY(!scene.compositor.defaultSeat()->keyboardFocus());
        QVERIFY(!first(manager, "Desktop folder").value("active").toBool());
        auto *entry = visualItem(scene.view.contentItem(), "desktop-entry-Desktop note.txt");
        QVERIFY(entry);
        const auto before = entry->position();
        const auto press = entry->mapToScene(QPointF(50, 20)).toPoint();
        QTest::mousePress(&scene.view, Qt::LeftButton, {}, press);
        QTest::mouseMove(&scene.view, press + QPoint(-250, 90), 40);
        QTest::mouseRelease(&scene.view, Qt::LeftButton, {}, press + QPoint(-250, 90));
        QTRY_COMPARE(entry->position(), before + QPointF(-250, 90));
        QVERIFY(QFileInfo(scene.files.path() + "/profile/desktop.json").isFile());
        commandKey(&scene.view, Qt::Key_O, 24);
        QTRY_VERIFY_WITH_TIMEOUT(!first(manager, "Desktop note.txt").isEmpty(), 10000);
        const auto editorId = first(manager, "Desktop note.txt").value("id").toULongLong();
        QVERIFY(capture(scene.view, "native-desktop-files.png"));
        QTest::mouseClick(&scene.view, Qt::LeftButton, {}, QPoint(35, 690));
        QVERIFY(manager.desktopActive());
        QVERIFY(!scene.compositor.defaultSeat()->keyboardFocus());
        commandKey(&scene.view, Qt::Key_A, 30);
        QCOMPARE(items->selectionCount(), 4);
        for (char key : QByteArray("keys belong to the desktop"))
            QTest::keyClick(&scene.view, key);
        // A clean close proves background keyboard input did not dirty the editor.
        manager.closeWindow(editorId);
        QTRY_VERIFY(first(manager, "Desktop note.txt").isEmpty());
        QVERIFY(first(manager, "Save Changes?").isEmpty());
        QCOMPARE(bytes(note), QByteArray("Opened from the native desktop."));
        manager.closeWindow(finderId);
        QTRY_COMPARE(manager.rowCount(), 0);
        QVERIFY(manager.desktopActive());
        QCOMPARE(manager.applications().size(), 1);
    }
};
int main(int argc, char **argv)
{
    if (qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") != "1")
        return 2;
    QGuiApplication app(argc, argv);
    app.setFont(classicInterfaceFont());
    NativeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "native_tests.moc"
