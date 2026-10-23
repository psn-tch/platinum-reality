#include "session_lock.hpp"
#include "ext-session-lock-client.h"
#include <QWindow>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

using namespace QtWaylandClient;

namespace
{
// A Qt window's surface in the lock-surface role, sized by the compositor (as LayerShellQt
// does for layer surfaces). The protocol requires each buffer to match the configured size.
class LockSurface final : public QWaylandShellSurface
{
  public:
    LockSurface(ext_session_lock_v1 *lock, QWaylandWindow *window) : QWaylandShellSurface(window)
    {
        // The screen this window was given (QWindow::setScreen), not waylandScreen(): that
        // follows the outputs the surface has entered, and a lock surface has entered none yet,
        // so every window would ask for the same output (a protocol error).
        auto *qscreen = window->window()->screen();
        auto *screen = qscreen ? dynamic_cast<QWaylandScreen *>(qscreen->handle()) : nullptr;
        if (!lock || !screen || !screen->output())
            return;
        m_surface = ext_session_lock_v1_get_lock_surface(lock, window->wlSurface(), screen->output());
        ext_session_lock_surface_v1_add_listener(m_surface, &listener, this);
    }
    ~LockSurface() override
    {
        if (m_surface)
            ext_session_lock_surface_v1_destroy(m_surface);
    }
    bool isExposed() const override { return m_configured; }
    void applyConfigure() override { resizeFromApplyConfigure(m_size); }
    // Qt commits a new role once without a buffer; for a lock surface that is a protocol error
    // ("committed with a null buffer"). The compositor configures it without any commit.
    bool commitSurfaceRole() const override { return false; }

  private:
    static void configure(void *data, ext_session_lock_surface_v1 *surface, uint32_t serial,
                          uint32_t width, uint32_t height)
    {
        auto *self = static_cast<LockSurface *>(data);
        ext_session_lock_surface_v1_ack_configure(surface, serial);
        self->m_size = QSize(int(width), int(height));
        if (self->m_configured)
        {
            // Later configures are resizes: apply them when Qt is not painting.
            self->applyConfigureWhenPossible();
            return;
        }
        self->m_configured = true;
        self->resizeFromApplyConfigure(self->m_size);
        self->window()->updateExposure(); // isExposed() is now true: Qt may draw.
    }
    static constexpr ext_session_lock_surface_v1_listener listener = {configure};
    ext_session_lock_surface_v1 *m_surface = nullptr;
    QSize m_size;
    bool m_configured = false;
};
} // namespace

class SessionLock::Integration final : public QWaylandShellIntegration
{
  public:
    explicit Integration(SessionLock *owner) : m_owner(owner) {}
    bool initialize(QWaylandDisplay *) override { return true; }
    // After finished() there is no lock: the window gets no role (and is never shown).
    QWaylandShellSurface *createShellSurface(QWaylandWindow *window) override
    {
        return new LockSurface(m_owner->m_lock, window);
    }

  private:
    SessionLock *m_owner;
};

SessionLock::SessionLock(QObject *parent) : QObject(parent)
{
    auto *integration = QWaylandIntegration::instance();
    if (!integration || !integration->display())
        return;
    for (const auto &global : integration->display()->globals())
        if (global.interface == QLatin1String(ext_session_lock_manager_v1_interface.name))
        {
            m_manager = static_cast<ext_session_lock_manager_v1 *>(
                wl_registry_bind(global.registry, global.id, &ext_session_lock_manager_v1_interface, 1));
            break;
        }
}

SessionLock::~SessionLock()
{
    // A held lock is never released here: only unlock() ends it. Destroying a locked lock
    // object is a protocol error, and the compositor keeps the outputs locked when we exit.
    if (m_lock && !m_locked)
        ext_session_lock_v1_destroy(m_lock);
    if (m_manager)
        ext_session_lock_manager_v1_destroy(m_manager);
}

bool SessionLock::lock()
{
    if (!m_manager || m_lock)
        return false;
    static constexpr ext_session_lock_v1_listener listener = {handleLocked, handleFinished};
    m_lock = ext_session_lock_manager_v1_lock(m_manager);
    ext_session_lock_v1_add_listener(m_lock, &listener, this);
    m_integration = std::make_unique<Integration>(this);
    return true;
}

bool SessionLock::cover(QWindow *window)
{
    if (!m_integration)
        return false;
    window->create();
    auto *waylandWindow = dynamic_cast<QWaylandWindow *>(window->handle());
    if (!waylandWindow)
        return false;
    waylandWindow->setShellIntegration(m_integration.get());
    return true;
}

bool SessionLock::unlock()
{
    if (!m_lock || !m_locked)
        return false;
    ext_session_lock_v1_unlock_and_destroy(m_lock);
    m_lock = nullptr;
    m_locked = false;
    wl_display_roundtrip(QWaylandIntegration::instance()->display()->wl_display());
    return true;
}

void SessionLock::handleLocked(void *data, ext_session_lock_v1 *)
{
    auto *self = static_cast<SessionLock *>(data);
    self->m_locked = true;
    emit self->locked();
}

void SessionLock::handleFinished(void *data, ext_session_lock_v1 *lock)
{
    auto *self = static_cast<SessionLock *>(data);
    // Refused, or ended by the compositor: the object may (and must) now be destroyed.
    ext_session_lock_v1_destroy(lock);
    self->m_lock = nullptr;
    self->m_locked = false;
    emit self->finished();
}
