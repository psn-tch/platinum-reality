#pragma once
#include <QObject>
#include <memory>

class QWaylandCompositor;

// Qt 6.12.0 routes to one wl_keyboard resource. Wayland clients may bind more
// than one keyboard for the same seat (Firefox does). Keep all those resources
// informed, without changing focus policy or recording key contents.
class KeyboardBroadcast final : public QObject
{
  public:
    KeyboardBroadcast(QWaylandCompositor *, QObject *parent);
    ~KeyboardBroadcast() override;
  private:
    struct State;
    std::unique_ptr<State> m_state;
};
