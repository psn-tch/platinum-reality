#pragma once
#include <QObject>
#include <map>
#include <memory>

class QWaylandCompositor;
struct wl_protocol_logger;
struct wl_resource;
struct wl_global;

// Modern pointers for every client. Qt 6.12.0 announces wl_seat version 4 (qwaylandseat.cpp) and
// its pointer never sends wl_pointer.frame. Chromium gathers scroll steps until a frame, so at
// version 4 it never scrolled (psn_tch, 2026-10-22). The seat is announced again at version 8, and
// each group of pointer events sent to a version 5+ pointer (by Qt or by this desktop) is closed
// with a frame before the next host pointer event is delivered, or before the event loop waits.
class PointerFrames final : public QObject
{
  public:
    static constexpr int seatVersion = 8;
    // After QWaylandCompositor::create(), before any client binds the seat.
    explicit PointerFrames(QWaylandCompositor *compositor);
    // False for Qt's original version 4 seat, which clients must not see. Global filters compose this.
    static bool advertised(const wl_global *global);
    ~PointerFrames() override;

  protected:
    // Before each host pointer event reaches Qt, the previous event's group is closed with a frame:
    // a press and its release are separate groups, as GTK requires. Qt then delivers the event as
    // usual (it is never re-sent: drags depend on Qt's own, spontaneous delivery), and a queued
    // flush frames it as soon as that delivery is over.
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    struct Pending;
    bool m_flushQueued = false;
    static void observe(void *data, int type, const void *message);
    void flush();
    QWaylandCompositor *m_compositor;
    wl_protocol_logger *m_logger = nullptr;
    wl_global *m_seat = nullptr;
    std::map<wl_resource *, std::unique_ptr<Pending>> m_pending;
};
