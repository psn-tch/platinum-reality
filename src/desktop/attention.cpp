#include "attention.hpp"
#include "xdg-activation-server.h"
#include "xdg-system-bell-server.h"
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QUuid>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>
#include <new>
#include <wayland-server-core.h>

struct ApplicationAttention::State
{
    struct Token
    {
        bool granted = false;
        QElapsedTimer age;
        // An application's token is good only while the keyboard focus it was issued under
        // lasts: closing a dialog, Firefox asked to bring its main window back, and that late
        // request then covered the next dialog (psn_tch, 2026-10-22). The desktop's own tokens (a
        // notification's action) are not tied to focus.
        bool focusBound = false;
        QPointer<QWaylandSurface> focus;
    };
    QWaylandCompositor *compositor = nullptr;
    wl_global *activation = nullptr;
    wl_global *bell = nullptr;
    QHash<QString, Token> tokens;

    static State *of(ApplicationAttention *owner) { return owner ? owner->m_state.get() : nullptr; }

    QString issue(bool granted, QWaylandSurface *focus = nullptr)
    {
        // Tokens are single-use and short-lived; keep the table small.
        for (auto it = tokens.begin(); it != tokens.end();)
            it = it->age.hasExpired(10000) ? tokens.erase(it) : std::next(it);
        while (tokens.size() >= 256)
            tokens.erase(tokens.begin());
        const auto token = QUuid::createUuid().toString(QUuid::Id128) +
                           QUuid::createUuid().toString(QUuid::Id128);
        Token entry{granted, {}, focus != nullptr, focus};
        entry.age.start();
        tokens.insert(token, entry);
        return token;
    }
    bool redeem(const QString &token, QWaylandSurface *focus)
    {
        const auto found = tokens.constFind(token);
        if (found == tokens.cend())
            return false;
        const bool valid = found->granted && !found->age.hasExpired(10000) &&
                           (!found->focusBound || (found->focus && found->focus == focus));
        tokens.erase(found);
        return valid;
    }
};

namespace
{
// Per-resource link back to the desktop, which may be gone before its clients.
struct Link
{
    QPointer<ApplicationAttention> owner;
};
struct TokenRequest
{
    QPointer<ApplicationAttention> owner;
    QPointer<QWaylandSurface> surface;
    bool committed = false;
};

void destroyResource(wl_client *, wl_resource *resource)
{
    wl_resource_destroy(resource);
}
void deleteLink(wl_resource *resource)
{
    delete static_cast<Link *>(wl_resource_get_user_data(resource));
}

void setSerial(wl_client *, wl_resource *, uint32_t, wl_resource *)
{
    // The keyboard focus at commit decides; serials are not tracked per event.
}
void setAppId(wl_client *, wl_resource *, const char *) {}
void setSurface(wl_client *, wl_resource *resource, wl_resource *surface)
{
    static_cast<TokenRequest *>(wl_resource_get_user_data(resource))->surface =
        QWaylandSurface::fromResource(surface);
}
void commit(wl_client *client, wl_resource *resource)
{
    auto *request = static_cast<TokenRequest *>(wl_resource_get_user_data(resource));
    if (request->committed)
    {
        wl_resource_post_error(resource, XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED,
                               "the activation token was already committed");
        return;
    }
    request->committed = true;
    auto *state = ApplicationAttention::State::of(request->owner);
    if (!state)
        return;
    // Granted only to the application that has the keyboard, for itself.
    auto *seat = state->compositor->defaultSeat();
    auto *focus = seat ? seat->keyboardFocus() : nullptr;
    const bool granted = focus && focus->client() && focus->client()->client() == client &&
                         (!request->surface || request->surface->client() == focus->client());
    xdg_activation_token_v1_send_done(resource, state->issue(granted, focus).toLatin1().constData());
}
const struct ::xdg_activation_token_v1_interface tokenRequests = {setSerial, setAppId, setSurface,
                                                                  commit, destroyResource};
void deleteTokenRequest(wl_resource *resource)
{
    delete static_cast<TokenRequest *>(wl_resource_get_user_data(resource));
}

void getActivationToken(wl_client *client, wl_resource *resource, uint32_t id)
{
    auto *object = wl_resource_create(client, &xdg_activation_token_v1_interface,
                                      wl_resource_get_version(resource), id);
    auto *request = object ? new (std::nothrow) TokenRequest : nullptr;
    if (!request)
    {
        if (object)
            wl_resource_destroy(object);
        wl_client_post_no_memory(client);
        return;
    }
    request->owner = static_cast<Link *>(wl_resource_get_user_data(resource))->owner;
    wl_resource_set_implementation(object, &tokenRequests, request, deleteTokenRequest);
}
void activate(wl_client *, wl_resource *resource, const char *token, wl_resource *surface)
{
    auto owner = static_cast<Link *>(wl_resource_get_user_data(resource))->owner;
    auto *state = ApplicationAttention::State::of(owner);
    auto *target = QWaylandSurface::fromResource(surface);
    if (!state || !target)
        return;
    auto *seat = state->compositor->defaultSeat();
    if (state->redeem(QString::fromLatin1(token), seat ? seat->keyboardFocus() : nullptr))
        emit owner->activationGranted(target);
    else
        emit owner->attentionRequested(target);
}
const struct ::xdg_activation_v1_interface activationRequests = {destroyResource, getActivationToken,
                                                                 activate};

void ring(wl_client *, wl_resource *resource, wl_resource *surface)
{
    auto owner = static_cast<Link *>(wl_resource_get_user_data(resource))->owner;
    if (owner)
        emit owner->bellRung(surface ? QWaylandSurface::fromResource(surface) : nullptr);
}
const struct ::xdg_system_bell_v1_interface bellRequests = {destroyResource, ring};

template <const wl_interface *Interface, auto Implementation>
void bind(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto *object = wl_resource_create(client, Interface, int(version), id);
    auto *link = object ? new (std::nothrow) Link{static_cast<ApplicationAttention *>(data)} : nullptr;
    if (!link)
    {
        if (object)
            wl_resource_destroy(object);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(object, Implementation, link, deleteLink);
}
} // namespace

ApplicationAttention::ApplicationAttention(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent), m_state(std::make_unique<State>())
{
    m_state->compositor = compositor;
    m_state->activation =
        wl_global_create(compositor->display(), &xdg_activation_v1_interface, 1, this,
                         bind<&xdg_activation_v1_interface, &activationRequests>);
    m_state->bell = wl_global_create(compositor->display(), &xdg_system_bell_v1_interface, 1, this,
                                     bind<&xdg_system_bell_v1_interface, &bellRequests>);
}

ApplicationAttention::~ApplicationAttention()
{
    if (m_state->activation)
        wl_global_destroy(m_state->activation);
    if (m_state->bell)
        wl_global_destroy(m_state->bell);
}

QString ApplicationAttention::grantedToken()
{
    return m_state->issue(true);
}
