#include "output_geometry.hpp"
#include "pointer_frames.hpp"
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QTimer>
#include <QWaylandCompositor>
#include <QWaylandQuickItem>
#include <QWaylandQuickOutput>
#include <QWaylandSurface>
#include <QWaylandView>
#include <QtWaylandCompositor/private/qwaylandoutput_p.h>
#include <wayland-server-core.h>
#include <QtMath>

struct DesktopOutputGeometry::Output
{
    std::unique_ptr<QWaylandQuickOutput> output;
    // Unregister from the manager before destroying wl_output.
    std::unique_ptr<QWaylandXdgOutputV1> logical;
    wl_global *global = nullptr;
};
struct DesktopOutputGeometry::Membership
{
    // Extra membership for a surface straddling monitors. These do not render
    // textures or become the surface's primary view. Qt owns enter/leave and
    // sends frame callbacks through the real rendering view only.
    std::map<QWaylandOutput *, std::unique_ptr<QWaylandView>> additional;
};

DesktopOutputGeometry::DesktopOutputGeometry(QWaylandCompositor *compositor, QQuickWindow *view)
    : m_compositor(compositor), m_view(view), m_manager(compositor)
{
    // This compositor currently has one trust domain and no other global
    // filter. This observer permits everything; future security filtering must
    // compose here, not replace it. Qt exposes no public wl_output global handle.
    wl_display_set_global_filter(compositor->display(), &DesktopOutputGeometry::observeGlobal, this);
    m_manager.initialize();
    connect(view, &QWindow::widthChanged, this, &DesktopOutputGeometry::synchronize);
    connect(view, &QWindow::heightChanged, this, &DesktopOutputGeometry::synchronize);
    // Main thread, after QML geometry/visibility changes and before the renderer
    // synchronizes. Iterate only Wayland surface views, not the whole QML tree.
    connect(view, &QQuickWindow::afterAnimating, this, [this] {
        if (m_layout)
            reconcileSurfaces();
    });
    synchronize();
}

DesktopOutputGeometry::~DesktopOutputGeometry()
{
    wl_display_set_global_filter(m_compositor->display(), nullptr, nullptr);
}

bool DesktopOutputGeometry::observeGlobal(const wl_client *, const wl_global *global, void *data)
{
    if (!PointerFrames::advertised(global))
        return false; // Composed: Qt's version 4 seat (pointer_frames.hpp).
    auto *self = static_cast<DesktopOutputGeometry *>(data);
    if (QByteArray(wl_global_get_interface(global)->name) != "wl_output")
        return true;
    for (auto &[name, entry] : self->m_outputs)
        if (entry && static_cast<QtWaylandServer::wl_output *>(QWaylandOutputPrivate::get(entry->output.get())) ==
                         wl_global_get_user_data(global))
            entry->global = const_cast<wl_global *>(global);
    if (self->m_layout && !self->m_bindUpdatePending)
    {
        self->m_bindUpdatePending = true;
        // Called for announcement/bind. Reconcile after the bind handler has
        // created the resource. Clients may ignore outputs entirely; do not
        // poll forever or keep their session rendering just to wait for a bind.
        QTimer::singleShot(0, self, [self] {
            self->m_bindUpdatePending = false;
            if (self->m_view) self->m_view->update();
        });
    }
    return true;
}

void DesktopOutputGeometry::outputWithdrawn(wl_global *global)
{
    auto *protocol = static_cast<QtWaylandServer::wl_output *>(wl_global_get_user_data(global));
    auto *output = static_cast<QWaylandOutputPrivate *>(protocol)->q_func();
    output->setProperty("platinumWithdrawn", true);
    auto *owner = static_cast<DesktopOutputGeometry *>(output->property("platinumOutputOwner").value<QObject *>());
    // The callback may run synchronously inside wl_global_remove(). Delete only
    // after retirement has moved the object into its owning collection.
    QTimer::singleShot(0, owner, [owner] { owner->pruneRetiredOutputs(); });
}

void DesktopOutputGeometry::pruneRetiredOutputs()
{
    std::erase_if(m_retired, [](const auto &entry) {
        return entry->output->property("platinumWithdrawn").toBool();
    });
}

bool DesktopOutputGeometry::setDisplayLayout(const DisplayLayout &layout)
{
    if (layout.mode() != DisplayLayout::Mode::Extend)
        return false; // Mirror's input/rendering contract is not production-ready.
    pruneRetiredOutputs();
    // Legacy clients can retain a registry without ack_global_remove. Keep its
    // inert globals until that registry dies, with an explicit resource ceiling.
    if (m_retired.size() + m_outputs.size() > 512)
        return false;
    m_ready = false;
    m_layout = layout;
    synchronize();
    if (m_view)
        m_view->update();
    return true;
}

void DesktopOutputGeometry::synchronize()
{
    if (!m_view || m_view->size().isEmpty())
        return;
    const auto layout = m_layout.value_or(DisplayLayout::single(m_view->size()));
    const auto refresh = m_view->screen() ? qFloor(m_view->screen()->refreshRate() * 1000) : 60000;
    QSet<QString> retained;
    for (const auto &description : layout.outputs())
    {
        retained.insert(description.name);
        auto &entry = m_outputs[description.name];
        if (!entry)
        {
            entry = std::make_unique<Output>();
            entry->output = std::make_unique<QWaylandQuickOutput>(m_compositor, m_view);
            entry->output->setSizeFollowsWindow(false);
            entry->output->setProperty("platinumDisplayName", description.name);
            entry->output->setProperty("platinumOutputOwner", QVariant::fromValue<QObject *>(this));
            entry->logical = std::make_unique<QWaylandXdgOutputV1>(entry->output.get(), &m_manager);
            entry->logical->setName(description.name);
            entry->logical->setDescription("Platinum Reality " + description.name);
            // QuickOutput's queued initialization adds a physical-window mode.
            // Reconcile afterward, retaining logical units as the current mode.
            QTimer::singleShot(0, this, &DesktopOutputGeometry::synchronize);
        }
        auto *output = entry->output.get();
        output->setPosition(description.logical.topLeft());
        entry->logical->setLogicalPosition(description.logical.topLeft());
        entry->logical->setLogicalSize(description.logical.size());
        // One surface unit = one desktop unit (ClientSurface.qml). Host scale
        // and rotation are already applied. Do not make SDL infer pixel density
        // from physical-mode/logical-size ratios (the earlier Ymir defect).
        if (output->currentMode().size() != description.logical.size())
        {
            const QWaylandOutputMode mode(description.logical.size(), refresh);
            output->addMode(mode, true);
            output->setCurrentMode(mode);
        }
    }
    // New outputs initialize from Qt's queued polish. Publish all replacements
    // before withdrawing old ones; a transient zero-screen registry can make
    // Qt clients unmap/recreate every window or drop fullscreen state.
    for (const auto &description : layout.outputs())
        if (!QWaylandOutputPrivate::get(m_outputs.at(description.name)->output.get())->isGlobal())
            return;
    m_compositor->setDefaultOutput(m_outputs.at(layout.primary())->output.get());
    // Retire memberships before outputs. Qt clears rendering views' output
    // pointers when wl_output disappears; reconcile them on the next frame.
    for (auto it = m_outputs.begin(); it != m_outputs.end();)
    {
        if (retained.contains(it->first))
        {
            ++it;
            continue;
        }
        for (auto &[surface, membership] : m_memberships)
            membership->additional.erase(it->second->output.get());
        auto *old = it->second->output.get();
        for (auto *surface : m_compositor->surfaces())
            for (auto *view : surface->views())
                if (view->output() == old)
                    view->setOutput(nullptr);
        if (it->second->global)
        {
            old->setProperty("platinumRetired", true);
            disconnect(m_view, nullptr, old, nullptr);
            // Qt forbids changing window() after initialization. The active
            // default output was moved to the head of the compositor's list
            // above, so outputFor(sharedWindow) cannot select this tombstone.
            wl_global_set_withdrawn_listener(it->second->global, &DesktopOutputGeometry::outputWithdrawn);
            wl_global_remove(it->second->global);
            m_retired.push_back(std::move(it->second));
        }
        it = m_outputs.erase(it);
    }
    if (!m_ready)
    {
        m_ready = true;
        emit layoutReady();
    }
}

void DesktopOutputGeometry::reconcileSurfaces()
{
    if (!m_view || !m_layout)
        return;
    QSet<QWaylandSurface *> retained;
    for (auto *surface : m_compositor->surfaces())
    {
        QList<QPair<QWaylandQuickItem *, QWaylandOutput *>> rendering;
        QSet<QWaylandOutput *> wanted;
        for (auto *view : surface->views())
        {
            auto *item = qobject_cast<QWaylandQuickItem *>(view->renderObject());
            if (!item || item->window() != m_view)
                continue;
            QRectF rect = item->mapRectToScene(item->boundingRect());
            if (!item->isVisible() || !surface->hasContent())
                rect = {};
            for (auto *ancestor = item->parentItem(); ancestor; ancestor = ancestor->parentItem())
                if (ancestor->clip())
                    rect = rect.intersected(ancestor->mapRectToScene(ancestor->boundingRect()));
            QWaylandOutput *dominant = nullptr;
            qreal largest = 0;
            for (const auto &description : m_layout->outputs())
            {
                const auto intersection = rect.intersected(description.logical);
                const auto area = intersection.width() * intersection.height();
                if (intersection.isEmpty())
                    continue;
                auto *output = m_outputs.at(description.name)->output.get();
                // Qt marks enter as sent even when this client has not bound
                // the newly announced output yet. Defer membership until it
                // can actually receive the event (notably during hotplug).
                if (!output->resourceForClient(surface->client()))
                {
                    continue;
                }
                wanted.insert(output);
                if (area > largest || (area == largest && description.name == m_layout->primary()))
                {
                    largest = area;
                    dominant = output;
                }
            }
            rendering.append({item, dominant});
        }
        if (rendering.isEmpty())
            continue;
        retained.insert(surface);
        auto &membership = m_memberships[surface];
        if (!membership)
            membership = std::make_unique<Membership>();
        // Keep every intersection referenced while changing the rendering
        // output. Otherwise moving the dominant view within a straddled window
        // could send a spurious leave/enter pair on a still-visible monitor.
        for (const auto &[item, output] : rendering)
            wanted.remove(output); // Extra views only for the other intersections.
        for (auto *output : wanted)
            if (!membership->additional.contains(output))
            {
                auto extra = std::make_unique<QWaylandView>();
                extra->setOutput(output);
                extra->setSurface(surface);
                membership->additional.emplace(output, std::move(extra));
            }
        for (const auto &[item, output] : rendering)
            item->setOutput(output);
        for (auto it = membership->additional.begin(); it != membership->additional.end();)
            if (!wanted.contains(it->first))
                it = membership->additional.erase(it);
            else
                ++it;
    }
    for (auto it = m_memberships.begin(); it != m_memberships.end();)
        if (!retained.contains(it->first))
            it = m_memberships.erase(it);
        else
            ++it;
}
