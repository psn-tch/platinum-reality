#pragma once
#include <QObject>
#include <memory>

class QWaylandCompositor;

// The primary selection (zwp_primary_selection_v1), which Qt's compositor lacks:
// select text in one application, middle-click to paste it in another, as on
// every Linux desktop. Like the clipboard, it is offered to the application that
// has the keyboard, and again whenever the keyboard moves. It is not kept after
// the selecting application quits, matching other desktops.
class PrimarySelection final : public QObject
{
  public:
    PrimarySelection(QWaylandCompositor *, QObject *parent);
    ~PrimarySelection() override;

    struct State;

  private:
    std::unique_ptr<State> m_state;
};
