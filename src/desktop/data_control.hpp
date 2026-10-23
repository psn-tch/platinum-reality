#pragma once
#include <QObject>
#include <memory>

class Clipboard;
class QWaylandCompositor;

// Clipboard tools (ext-data-control-v1): wl-copy and wl-paste, which terminal programs use, and
// clipboard managers. They read and set the clipboard without a window or the keyboard. Without
// this, wl-copy made a window to get focus and then kept running to serve its data, so the
// desktop left "wl-clipboard" active and no application had the keyboard.
//
// A tool's copy is read in full (bounded), installed as the clipboard the way a shell copy is,
// and its source is then cancelled, so wl-copy exits. The primary selection is not offered to
// tools; their primary sources are cancelled at once, rather than left waiting.
class DataControl final : public QObject
{
  public:
    DataControl(QWaylandCompositor *, Clipboard *, QObject *parent);
    ~DataControl() override;

    struct State;

  private:
    std::unique_ptr<State> m_state;
};
