#include "wallet_activation.hpp"
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

WalletActivation::WalletActivation(QDBusConnection bus, QObject *parent)
    : QObject(parent), m_bus(std::move(bus))
{
    qDBusRegisterMetaType<QMap<QString, QString>>();
    m_watch.setInterval(250);
    connect(&m_watch, &QTimer::timeout, this, [this] {
        if ((!m_pending && !m_ready) || m_bus.isConnected())
            return;
        ++m_generation;
        m_pending = m_ready = false;
        m_error = "The session wallet connection was lost.";
        m_watch.stop();
        emit changed();
    });
}

QMap<QString, QString> WalletActivation::activationEnvironment(const QProcessEnvironment &environment)
{
    QMap<QString, QString> result;
    // A short allowlist avoids copying application tokens/credentials into service
    // activation. Empty values replace inherited host display/style settings too.
    for (const auto *key : {"HOME", "PATH", "LANG", "LC_ALL", "LC_MESSAGES",
                           "XDG_RUNTIME_DIR", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME",
                           "XDG_STATE_HOME", "XDG_DATA_DIRS", "XDG_CURRENT_DESKTOP", "XDG_SESSION_DESKTOP",
                           "XDG_SESSION_TYPE", "DBUS_SESSION_BUS_ADDRESS", "WAYLAND_DISPLAY", "DISPLAY",
                           "XAUTHORITY", "QT_QPA_PLATFORM", "QT_QPA_PLATFORMTHEME", "QT_PLUGIN_PATH",
                           "QT_STYLE_OVERRIDE", "QT_WAYLAND_CLIENT_BUFFER_INTEGRATION",
                           "QT_WAYLAND_DISABLE_WINDOWDECORATION", "GDK_BACKEND", "GSETTINGS_BACKEND"})
        result.insert(QString::fromLatin1(key), environment.value(QString::fromLatin1(key)));
    // GIO's MIME loader does not apply the XDG default to an exported empty
    // value. Keep system icons/schemas available without importing bus services.
    if (result.value("XDG_DATA_DIRS").isEmpty())
        result.insert("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
    // Replace an inherited outer descriptor; the service supervisor must then
    // remove this key entirely, since an empty value is invalid to libwayland.
    result.insert("WAYLAND_SOCKET", "");
    return result;
}

void WalletActivation::configure(const QProcessEnvironment &environment)
{
    const auto generation = ++m_generation;
    m_pending = true;
    m_ready = false;
    m_error.clear();
    if (!m_bus.isConnected()) {
        m_pending = false;
        m_error = "The session bus is unavailable for saved sign-ins.";
        emit changed();
        return;
    }
    m_watch.start();
    emit changed();
    auto call = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", "UpdateActivationEnvironment");
    call << QVariant::fromValue(activationEnvironment(environment));
    auto *update = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 5000), this);
    connect(update, &QDBusPendingCallWatcher::finished, this, [this, update, generation] {
        update->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusPendingReply<> reply = *update;
        if (reply.isError()) {
            m_pending = false;
            m_error = "The wallet prompt could not be connected to this desktop.";
            emit changed();
            return;
        }
        // Verify the allowlist is installed without activating any real service.
        auto query = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus", "ListActivatableNames");
        auto *names = new QDBusPendingCallWatcher(m_bus.asyncCall(query, 5000), this);
        connect(names, &QDBusPendingCallWatcher::finished, this, [this, names, generation] {
            names->deleteLater();
            if (generation != m_generation)
                return;
            const QDBusPendingReply<QStringList> reply = *names;
            const auto available = reply.isError() ? QStringList{} : reply.value();
            m_pending = false;
            m_ready = available.contains("org.kde.kwalletd6") &&
                      available.contains("org.kde.secretservicecompat") &&
                      available.contains("org.kde.secretprompter");
            if (!m_ready)
                m_error = "The KDE wallet services are unavailable in this session.";
            emit changed();
        });
    });
}
