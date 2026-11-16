#pragma once
#include <QObject>
#include <QProcess>
#include <QElapsedTimer>
#include <QList>
#include <QTimer>

// Owns a private Xwayland/XWM pair. X windows arrive as ordinary xdg surfaces;
// NativeManager retains all desktop policy. Never connects apps to the host X
// server. This is compatibility, not an isolation boundary between X clients.
class X11Bridge final : public QObject
{
    Q_OBJECT
  public:
    explicit X11Bridge(QObject *parent = nullptr);
    ~X11Bridge() override;
    void start(const QByteArray &waylandSocket);
    void stop();
    bool starting() const { return m_starting; }
    bool ready() const { return !m_display.isEmpty(); }
    QString display() const { return m_display; }
    QString state() const;
    // The authenticated Wayland peer for our own persistent X11 bridge, not an
    // X11 application's claimed ID or process metadata.
    bool ownsWaylandPeer(qint64 pid) const;
    void applyTo(QProcessEnvironment &environment) const;
    static QString helperPath();
    // The helper's process (tests stop it to check the restart).
    qint64 processId() const { return m_process.processId(); }
    // A bridge that stops after it was ready starts again, at most this often in restartWindowMs.
    static constexpr int maximumRestarts = 3, restartWindowMs = 10 * 60 * 1000;
  signals:
    void changed();
    void failed(QString message);
  private:
    void readOutput();
    void fail(const QString &message);
    void cleanRuntime();
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_pending;
    QString m_runtime, m_authority, m_display;
    QByteArray m_socket;
    bool m_starting = false, m_stopping = false, m_failed = false, m_wasReady = false;
    QList<qint64> m_restarts; // When each automatic restart happened (m_clock), newest last.
    QElapsedTimer m_clock;
    qsizetype m_logged = 0;
};

// Declare after DesktopClientRetirement and before the view: the view's render
// thread must join before terminating clients, but their server must still live.
struct X11BridgeRetirement final
{
    X11Bridge *bridge = nullptr;
    ~X11BridgeRetirement() { if (bridge) bridge->stop(); }
};
