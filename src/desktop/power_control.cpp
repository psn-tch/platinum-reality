#include "power_control.hpp"
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

PowerControl::PowerControl(QDBusConnection bus, QObject *parent) : QObject(parent), m_bus(std::move(bus))
{
}
bool PowerControl::available() const
{
    return m_bus.isConnected();
}
bool PowerControl::request(Action action)
{
    if (!available() || m_busy)
        return false;
    const auto method = action == Action::Sleep     ? "Suspend"
                        : action == Action::Restart ? "Reboot"
                                                    : "PowerOff";
    auto message = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
                                                  "org.freedesktop.login1.Manager", method);
    // Interactive: polkit may ask for a password where its policy wants one.
    message << true;
    m_busy = true;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 30000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, action]
    {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        m_busy = false;
        if (!reply.isError())
        {
            emit finished(true, {});
            return;
        }
        const auto what = action == Action::Sleep     ? "The computer could not go to sleep."
                          : action == Action::Restart ? "The computer could not restart."
                                                      : "The computer could not shut down.";
        emit finished(false, QString(what) + " " + reply.error().message());
    });
    return true;
}
