#pragma once
#include <QObject>
#include <memory>

class QWaylandCompositor;
class QWaylandSurface;

// Applications asking to come forward (xdg-activation-v1) or ringing the bell
// (xdg-system-bell-v1). Neither is in Qt's compositor.
//
// Activation follows the usual focus-stealing prevention: a token counts only
// if the application that asked for it had the keyboard when it asked, and only
// once, within a short time. A link opened from the active application can then
// bring the browser forward. Any other request never takes the keyboard. The
// application is marked as wanting attention instead, as Mac OS 9's
// Notification Manager did (a diamond in the Application menu).
class ApplicationAttention final : public QObject
{
    Q_OBJECT
  public:
    ApplicationAttention(QWaylandCompositor *, QObject *parent);
    ~ApplicationAttention() override;
    // A token the desktop grants itself, e.g. for an application it launches.
    QString grantedToken();

  signals:
    void activationGranted(QWaylandSurface *surface);
    void attentionRequested(QWaylandSurface *surface);
    // surface may be null: the bell rang for the whole application.
    void bellRung(QWaylandSurface *surface);

  public:
    struct State;

  private:
    std::unique_ptr<State> m_state;
};
