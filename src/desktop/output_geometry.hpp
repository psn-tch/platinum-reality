#pragma once
#include <QObject>
#include <QPointer>
#include <QWaylandXdgOutputManagerV1>
#include <QWaylandXdgOutputV1>
#include "display_layout.hpp"
#include <map>
#include <memory>
#include <vector>

class QWaylandCompositor;
class QWaylandOutput;
class QQuickWindow;
class QWaylandSurface;
struct wl_client;
struct wl_global;

// Describes the desktop output in the units clients are presented in, which is
// how they size and centre windows: one surface unit is one desktop unit (see
// ClientSurface.qml). Both the current mode and the standard xdg-output logical
// size are the desktop's logical size; the host's scale is the desktop's own.
// Before this, Qt advertised window pixels, stale at 2x after the host's early
// integer scale: clients believed a 2560x1440 desktop was 5120x2880.
// Owns its outputs; destroy after the renderer, before the compositor.
class DesktopOutputGeometry final : public QObject
{
    Q_OBJECT
  public:
    DesktopOutputGeometry(QWaylandCompositor *compositor, QQuickWindow *view);
    ~DesktopOutputGeometry() override;
    void synchronize();
    // Opt-in Extend only. The host still renders one spanning window; clients
    // see actual logical monitors, never an extra canvas-sized output.
    bool setDisplayLayout(const DisplayLayout &layout);
    bool ready() const { return m_ready; }
  signals:
    void layoutReady();

  private:
    struct Output;
    struct Membership;
    void reconcileSurfaces();
    static bool observeGlobal(const wl_client *, const wl_global *, void *);
    static void outputWithdrawn(wl_global *);
    void pruneRetiredOutputs();
    QWaylandCompositor *m_compositor;
    QPointer<QQuickWindow> m_view; // Destroyed before this helper in main.cpp.
    QWaylandXdgOutputManagerV1 m_manager;
    // Unregisters from m_manager on destruction, so it is declared after it.
    std::map<QString, std::unique_ptr<Output>> m_outputs;
    std::map<QWaylandSurface *, std::unique_ptr<Membership>> m_memberships;
    std::vector<std::unique_ptr<Output>> m_retired;
    std::optional<DisplayLayout> m_layout;
    bool m_bindUpdatePending = false;
    bool m_ready = false;
};
