#include "x11_bridge.hpp"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cstring>
#include <unistd.h>

namespace {
bool writeAuthority(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QDataStream stream(&file); // Xauthority uses network-order 16-bit lengths.
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint16(65535); // FamilyWild; this file is private to this server.
    const auto field = [&](const QByteArray &value) {
        stream << quint16(value.size());
        stream.writeRawData(value.constData(), value.size());
    };
    field({}); // address
    field({}); // any display number: Xwayland chooses an unused number later.
    field("MIT-MAGIC-COOKIE-1");
    QByteArray cookie(16, Qt::Uninitialized);
    for (int i = 0; i < cookie.size(); i += 4) {
        const auto random = QRandomGenerator::system()->generate();
        memcpy(cookie.data() + i, &random, sizeof(random));
    }
    field(cookie);
    return stream.status() == QDataStream::Ok && file.flush();
}
}

X11Bridge::X11Bridge(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &X11Bridge::readOutput);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_stopping && error == QProcess::FailedToStart)
            fail("X11 support could not start: " + m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        m_deadline.stop();
        if (!m_stopping)
            fail(QString("X11 support stopped (%1, %2). Native Wayland applications remain available.")
                     .arg(code).arg(status == QProcess::NormalExit ? "exit" : "crash"));
        cleanRuntime();
    });
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(10000);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail("X11 support did not become ready within ten seconds.");
        stop();
    });
}
X11Bridge::~X11Bridge() { stop(); }

QString X11Bridge::helperPath()
{
#ifdef PLATINUM_X11_ENABLED
    return QCoreApplication::applicationDirPath() + "/xwayland-satellite";
#else
    return {};
#endif
}
QString X11Bridge::state() const
{
    return ready() ? "ready" : m_starting ? "starting" : m_failed ? "failed" : "disabled";
}
bool X11Bridge::ownsWaylandPeer(qint64 pid) const
{
    return pid > 0 && m_process.state() != QProcess::NotRunning && m_process.processId() == pid;
}
void X11Bridge::start(const QByteArray &waylandSocket)
{
    if (starting() || ready() || helperPath().isEmpty())
        return;
    m_stopping = false;
    m_failed = false;
    const auto parentRuntime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QFileInfo parent(parentRuntime);
    if (!parent.isDir() || parent.isSymLink() || parent.ownerId() != getuid()) {
        fail("X11 support requires the owned session runtime directory.");
        return;
    }
    QTemporaryDir runtime(QDir(parentRuntime).filePath("platinum-x11.XXXXXX"));
    if (!runtime.isValid()) {
        fail("X11 support could not create its private runtime.");
        return;
    }
    runtime.setAutoRemove(false); // Never recursively delete unexpected files.
    m_runtime = runtime.path();
    m_authority = m_runtime + "/authority";
    if (!writeAuthority(m_authority)) {
        fail("X11 support could not create its private authorization file.");
        cleanRuntime();
        return;
    }
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &key : {"DISPLAY", "XAUTHORITY", "WAYLAND_SOCKET", "LD_PRELOAD", "RUST_LOG"})
        environment.remove(key);
    const auto display = QString::fromUtf8(waylandSocket);
    environment.insert("WAYLAND_DISPLAY", QDir::isAbsolutePath(display) ? display
                                          : QDir(parentRuntime).filePath(display));
    environment.insert("XDG_RUNTIME_DIR", m_runtime);
    // Installed Xwayland, never an app wrapper, started through our launcher
    // that keeps X11 clients on implicit sync (xwayland_implicit_sync.c).
    const auto launcher = QCoreApplication::applicationDirPath() + "/xwayland";
    if (QFileInfo(launcher + "/Xwayland").isExecutable()) {
        environment.insert("PATH", launcher + ":/usr/bin:/bin");
    } else {
        qWarning().noquote() << "X11: private Xwayland launcher missing; X11 Vulkan clients may hang if the desktop exits";
        environment.insert("PATH", "/usr/bin:/bin");
    }
    environment.insert("RUST_LOG", "warn,xwayland_satellite=info,xwayland_process=error");
    m_process.setProcessEnvironment(environment);
    m_starting = true;
    emit changed();
    m_process.start(helperPath(), {"-nolisten", "tcp", "-auth", m_authority});
    m_deadline.start();
}
void X11Bridge::readOutput()
{
    m_pending += m_process.readAllStandardOutput();
    static const QRegularExpression readyLine("Connected to Xwayland on (:[0-9]+)(?:\\s|$)");
    while (m_pending.contains('\n')) {
        const auto index = m_pending.indexOf('\n');
        const auto line = m_pending.left(index);
        m_pending.remove(0, index + 1);
        if (m_logged < 65536) {
            qInfo().noquote() << "X11:" << QString::fromUtf8(line);
            m_logged += line.size();
        }
        // Version-pinned helper reports this only after Xwayland's displayfd
        // signals readiness. Never guess a display number or inherit KDE's.
        if (m_starting) {
            const auto match = readyLine.match(QString::fromUtf8(line));
            if (match.hasMatch()) {
                m_deadline.stop();
                m_display = match.captured(1);
                m_starting = false;
                emit changed();
            }
        }
    }
    if (m_pending.size() > 8192)
        m_pending = m_pending.right(8192);
}
void X11Bridge::applyTo(QProcessEnvironment &environment) const
{
    if (ready()) {
        environment.insert("DISPLAY", m_display);
        environment.insert("XAUTHORITY", m_authority);
    }
}
void X11Bridge::fail(const QString &message)
{
    m_deadline.stop();
    const bool firstFailure = !m_failed;
    m_failed = true;
    m_starting = false;
    m_display.clear();
    emit changed();
    if (firstFailure)
        emit failed(message);
}
void X11Bridge::stop()
{
    m_stopping = true;
    m_deadline.stop();
    m_starting = false;
    m_display.clear();
    if (m_process.state() != QProcess::NotRunning) {
        // Closing the helper closes the WM connection; its owned Xwayland then
        // exits too. Bounded waits only, never signal another session's server.
        m_process.terminate();
        if (!m_process.waitForFinished(2000)) {
            m_process.kill();
            m_process.waitForFinished(2000);
        }
    }
    cleanRuntime();
}
void X11Bridge::cleanRuntime()
{
    if (m_runtime.isEmpty())
        return;
    const QDir directory(m_runtime);
    static const QRegularExpression socketName("^xwls-([1-9][0-9]{0,2})(\\.lock)?$");
    for (const auto &file : directory.entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot)) {
        const auto name = file.fileName();
        if (file.isSymLink() || file.ownerId() != getuid())
            continue;
        const auto match = socketName.match(name);
        const bool ownedSocket = match.hasMatch() && match.captured(1).toInt() <= 128;
        // These are the only files we create or ask the pinned helper to create.
        // QFile::remove unlinks a socket; it never traverses a directory.
        if (name == "authority" || (ownedSocket &&
            (!name.endsWith(".lock") || (file.isFile() && file.size() == 0))))
            QFile::remove(file.absoluteFilePath());
    }
    if (!QDir().rmdir(m_runtime))
        qWarning().noquote() << "X11 runtime retained without recursive removal:" << m_runtime;
    m_runtime.clear();
    m_authority.clear();
}
