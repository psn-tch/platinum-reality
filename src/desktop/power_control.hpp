#pragma once
#include <QDBusConnection>
#include <QObject>

// Special ▸ Sleep, Restart and Shut Down through systemd-logind (org.freedesktop.login1), which
// asks polkit: an active local session is normally allowed without a password. Requests only;
// the desktop asks applications to close before Restart and Shut Down. Without a connected bus
// (previews, tests that are not given a private one) nothing is ever requested.
class PowerControl final : public QObject
{
    Q_OBJECT
  public:
    enum class Action { Sleep, Restart, ShutDown };
    explicit PowerControl(QDBusConnection bus, QObject *parent = nullptr);
    bool available() const;
    bool busy() const
    {
        return m_busy;
    }
    // False when unavailable or a request is already waiting for its answer.
    bool request(Action action);

  signals:
    void finished(bool ok, QString error);

  private:
    QDBusConnection m_bus;
    bool m_busy = false;
};
