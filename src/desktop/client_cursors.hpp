#pragma once
#include <QObject>
#include <memory>

class QWaylandCompositor;

// Shows each nested client's own cursor (a text beam, resize arrows, or none at all while a game
// has the pointer locked) on the window item under the pointer. Shared-memory cursor images are
// converted to a QCursor; anything else keeps the desktop arrow.
class ClientCursors final : public QObject
{
    Q_OBJECT
  public:
    explicit ClientCursors(QWaylandCompositor *compositor, QObject *parent = nullptr);
    ~ClientCursors() override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
