#pragma once
#include <QQuickView>
#include <QWaylandQuickCompositor>

class DesktopClientRetirement final
{
  public:
    explicit DesktopClientRetirement(QWaylandCompositor *compositor = nullptr)
        : m_compositor(compositor) {}
    DesktopClientRetirement(const DesktopClientRetirement &) = delete;
    DesktopClientRetirement &operator=(const DesktopClientRetirement &) = delete;
    void setCompositor(QWaylandCompositor *compositor) { m_compositor = compositor; }
    ~DesktopClientRetirement()
    {
        // Declare after protocol owners and before the view. The renderer has
        // joined, but the protocol objects needed by wl_client_destroy live on.
        if (!m_compositor)
            return;
        const auto connected = m_compositor->clients();
        if (!connected.isEmpty())
            qInfo("Retiring %lld nested client connection(s) after renderer shutdown",
                   static_cast<long long>(connected.size()));
        for (auto *client : connected)
            m_compositor->destroyClient(client);
        if (!connected.isEmpty())
            qInfo("Nested client retirement complete");
    }
  private:
    QWaylandCompositor *m_compositor;
};

// Clear GUI references and drain scene-graph resources while the manager,
// output and Wayland display still exist. The view itself must also be declared
// after the retirement guard and protocols, so it joins the renderer first.
inline void releaseDesktopView(QQuickView &view)
{
    view.setPersistentSceneGraph(false);
    view.setPersistentGraphics(false);
    view.setSource({});
    view.hide();
    view.releaseResources();
}
