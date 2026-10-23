#pragma once
#include "display_layout.hpp"
#include <QObject>
#include <memory>

class QQuickWindow;

// Optional, experiment-only touch adaptation. This does not own the mouse, pointer
// constraints, cursor presentation, drag/drop, or a production topology transaction.
class MirrorTouch final : public QObject
{
  public:
    explicit MirrorTouch(QQuickWindow *window);
    ~MirrorTouch() override;
    // Refuse replacement while an accepted contact is held. After cancel(), held
    // contacts remain rejected until lifted, even if the layout changes meanwhile.
    bool setLayout(const DisplayLayout &layout);
    bool hasActiveContacts() const;
    bool cancel();

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
