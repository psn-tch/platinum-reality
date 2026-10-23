#pragma once
#include <QObject>
#include <QPointF>
#include <QRegion>
#include <memory>

class QWindow;

// The desktop window's pointer as seen by the host compositor (the private Sway backend).
// Binds the host's relative-pointer and pointer-constraints globals on Qt's own Wayland
// connection, so they apply to the same seat and window surface Qt uses. Raw deltas only come
// from the host; nothing here derives motion from absolute positions.
class HostPointer final : public QObject
{
    Q_OBJECT
  public:
    explicit HostPointer(QWindow *window, QObject *parent = nullptr);
    ~HostPointer() override;
    // The host offers both protocols and the window is a Wayland window.
    bool available() const;
    // Keep the host cursor where it is; only relative motion reaches the desktop. False when
    // the seat has no pointer or the window has no surface yet.
    bool lock();
    // Keep the host cursor inside a region given in window coordinates.
    bool confine(const QRegion &region);
    void release();
    // The host reports the lock or confinement as active.
    bool engaged() const;
    // Optional desktop-level confinement, for a mirrored desktop's single pointer domain.
    // Empty disables it. A client lock/confinement temporarily takes precedence; release()
    // restores this region. It is not a client capture and does not change engaged().
    // Regions are host-window logical coordinates, never physical pixels.
    void setDesktopRegion(const QRegion &region);
    bool desktopConfinementEngaged() const;

  signals:
    void relativeMotion(quint64 microseconds, QPointF delta, QPointF unaccelerated);
    void engagedChanged(bool engaged);
    void desktopConfinementEngagedChanged(bool engaged);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
