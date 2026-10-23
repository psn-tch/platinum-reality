#pragma once
#include <QObject>
#include <memory>

class QWindow;

// An idle inhibitor on the desktop window at the host compositor (the private Sway backend),
// bound on Qt's own Wayland connection like HostPointer. Sway pauses its idle timers, and so
// swayidle's lock, while a visible surface holds one.
class HostIdle final : public QObject
{
    Q_OBJECT
  public:
    explicit HostIdle(QWindow *window, QObject *parent = nullptr);
    ~HostIdle() override;
    bool available() const;
    // Kept across window surface changes: the inhibitor is recreated on a new surface.
    void setInhibited(bool inhibited);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
