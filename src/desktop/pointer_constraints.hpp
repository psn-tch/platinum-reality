#pragma once
#include <QObject>
#include <memory>

class HostPointer;
class QWaylandCompositor;

// zwp_relative_pointer_manager_v1 and zwp_pointer_constraints_v1 for nested clients (games,
// emulators, and X11 programs through the bridge). Relative motion and the lock or confinement
// come from the host compositor through HostPointer; nothing is derived from absolute positions,
// and the globals are only advertised when the host supports both protocols.
//
// A constraint activates only while its surface has pointer focus, its client has keyboard
// focus, no shell menu or modal dialog is open, and the user has not just released it.
class PointerConstraints final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool captured READ captured NOTIFY capturedChanged)
  public:
    PointerConstraints(QWaylandCompositor *compositor, HostPointer *host, QObject *parent = nullptr);
    ~PointerConstraints() override;
    bool available() const;
    // A nested client's lock or confinement is active.
    bool captured() const;
    // Shell policy: false while a menu, modal dialog or desktop drag is in progress.
    void setPermitted(bool permitted);
    // The release chord: deactivate now and keep persistent constraints inactive until the
    // user clicks in a window again.
    void release();

  signals:
    void capturedChanged();

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
