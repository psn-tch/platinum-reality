#pragma once
#include <QDBusContext>
#include <QObject>
#include <memory>

class QWaylandCompositor;
class QWindow;

// Keeps the session from idling (and the lock screen from starting) while an application asks:
// nested Wayland clients through zwp_idle_inhibit_manager_v1 (video players, Firefox, SDL games)
// and session-bus clients through org.freedesktop.ScreenSaver Inhibit (Chromium, SDL on X11,
// Steam). Any active request becomes one idle inhibitor on the desktop window at the host,
// which Sway honours while the window is visible.
class IdleInhibition final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool inhibited READ inhibited NOTIFY inhibitedChanged)
  public:
    IdleInhibition(QWaylandCompositor *compositor, QWindow *window, bool connectBus,
                   QObject *parent = nullptr);
    ~IdleInhibition() override;
    bool inhibited() const;
    // The host offers zwp_idle_inhibit_manager_v1 and the protocol is advertised to clients.
    bool available() const;
    // User preference composes with, rather than replacing, application requests.
    void setUserKeepsAwake(bool keepAwake);
    // Session-bus requests (org.freedesktop.ScreenSaver).
    uint inhibit(const QString &owner, const QString &application, const QString &reason);
    void uninhibit(const QString &owner, uint cookie);

  signals:
    void inhibitedChanged();

  private:
    struct State;
    std::unique_ptr<State> m_state;
};

// org.freedesktop.ScreenSaver for the private session bus. Requests end with UnInhibit or when
// the caller leaves the bus.
class ScreenSaverAdaptor final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")
  public:
    explicit ScreenSaverAdaptor(IdleInhibition *owner);
  public slots:
    uint Inhibit(const QString &application, const QString &reason);
    void UnInhibit(uint cookie);
    bool GetActive();
    void SimulateUserActivity();

  private:
    IdleInhibition *m_owner;
};
