#pragma once
#include <QObject>
#include <memory>

class QWaylandCompositor;
class QWaylandSurface;

// GTK3 communicates modal toplevels through gtk_shell1, rather than xdg-dialog.
// No menu capabilities or timestamp-only focus-stealing permission are granted.
class GtkShell final : public QObject
{
    Q_OBJECT
  public:
    GtkShell(QWaylandCompositor *, QObject *parent);
    ~GtkShell() override;
    static bool surfaceModal(QWaylandSurface *);
    struct State;
    std::unique_ptr<State> m_state;
  signals:
    void modalityChanged(QWaylandSurface *);
    void bellRung();
};
