#include <QCoreApplication>
#include <QDBusConnection>
#include <QProcess>
#include <QTimer>
#include <csignal>
#include <sys/prctl.h>
#include <unistd.h>

// dbus-run-session owns the private bus, but activated services can otherwise outlive
// it. Keep each foreground wallet daemon attached to that bus's lifetime. No wallet
// is opened here; the installed KDE service still handles its own protocol/prompts.
namespace {
volatile std::sig_atomic_t stopping = 0;
void stop(int) { stopping = 1; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() < 3 || arguments.at(1) != "--" || !arguments.at(2).startsWith('/'))
        return 2;
    const auto address = qEnvironmentVariable("DBUS_STARTER_ADDRESS");
    if (address.isEmpty())
        return 2; // Only a service activated by our private bus may start a daemon.
    auto bus = QDBusConnection::connectToBus(address, "wallet-supervisor");
    if (!bus.isConnected())
        return 1;
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("DBUS_SESSION_BUS_ADDRESS", address);
    // UpdateActivationEnvironment can replace, but cannot unset, inherited keys.
    // Even an empty WAYLAND_SOCKET bypasses WAYLAND_DISPLAY in libwayland and
    // fails descriptor parsing. Activated GUI daemons must open the inner display.
    environment.remove("WAYLAND_SOCKET");
    child.setProcessEnvironment(environment);
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    const auto parent = getpid();
    child.setChildProcessModifier([parent] {
        // Also release the foreground child if this supervisor is killed abruptly.
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != parent)
            _exit(1);
    });
    std::signal(SIGTERM, stop);
    std::signal(SIGINT, stop);
    QTimer watch;
    watch.setInterval(100);
    bool closing = false;
    QObject::connect(&watch, &QTimer::timeout, &app, [&] {
        if (closing || (!stopping && bus.isConnected()))
            return;
        closing = true;
        child.terminate();
        QTimer::singleShot(3000, &child, [&] { if (child.state() != QProcess::NotRunning) child.kill(); });
    });
    QObject::connect(&child, &QProcess::finished, &app, [&](int code, QProcess::ExitStatus status) {
        app.exit(closing ? 0 : status == QProcess::NormalExit ? code : 1);
    });
    QObject::connect(&child, &QProcess::errorOccurred, &app, [&](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            app.exit(1);
    });
    child.start(arguments.at(2), arguments.mid(3));
    watch.start();
    return app.exec();
}
