#include "keychain_service.hpp"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QVariantMap>

#include <algorithm>
#include <memory>

namespace {
const QString service = "org.freedesktop.secrets";
const QString root = "/org/freedesktop/secrets";
const QString serviceInterface = "org.freedesktop.Secret.Service";
const QString collectionInterface = "org.freedesktop.Secret.Collection";

QDBusMessage getAll(const QString &path, const QString &interface)
{
    auto message = QDBusMessage::createMethodCall(service, path, "org.freedesktop.DBus.Properties", "GetAll");
    message << interface;
    return message;
}
}

KeychainService::KeychainService(QDBusConnection bus, QObject *parent) : QObject(parent), m_bus(std::move(bus))
{
}

void KeychainService::refresh()
{
    const int generation = ++m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(getAll(root, serviceInterface), 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        QDBusPendingReply<QVariantMap> reply = *watcher;
        QList<QDBusObjectPath> paths;
        if (!reply.isError())
            reply.value().value("Collections").value<QDBusArgument>() >> paths;
        m_available = !reply.isError();
        // "session" is the in-memory collection behind the "default" alias's neighbours: not a keychain.
        paths.erase(std::remove_if(paths.begin(), paths.end(),
                                   [](const QDBusObjectPath &p) { return p.path().endsWith("/session"); }),
                    paths.end());
        if (paths.isEmpty()) {
            m_keychains.clear();
            emit changed();
            return;
        }
        auto results = std::make_shared<QVariantList>();
        auto remaining = std::make_shared<int>(paths.size());
        for (const auto &path : paths) {
            auto *one = new QDBusPendingCallWatcher(m_bus.asyncCall(getAll(path.path(), collectionInterface), 3000), this);
            connect(one, &QDBusPendingCallWatcher::finished, this, [this, one, path, results, remaining, generation] {
                one->deleteLater();
                QDBusPendingReply<QVariantMap> properties = *one;
                if (!properties.isError())
                    results->append(QVariantMap{{"path", path.path()},
                                                {"label", properties.value().value("Label").toString()},
                                                {"locked", properties.value().value("Locked").toBool()}});
                if (--*remaining || generation != m_generation)
                    return;
                std::sort(results->begin(), results->end(), [](const QVariant &a, const QVariant &b) {
                    return a.toMap().value("label").toString().localeAwareCompare(b.toMap().value("label").toString()) < 0;
                });
                m_keychains = *results;
                emit changed();
            });
        }
    });
}

// Lock or Unlock returns the objects done at once and, if the password store must ask, a prompt
// to show; the prompt's Completed signal ends it. Either way the list is read again.
void KeychainService::call(const QString &method, const QStringList &paths)
{
    QList<QDBusObjectPath> objects;
    for (const auto &path : paths)
        objects.append(QDBusObjectPath(path));
    auto message = QDBusMessage::createMethodCall(service, root, serviceInterface, method);
    message << QVariant::fromValue(objects);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QList<QDBusObjectPath>, QDBusObjectPath> reply = *watcher;
        const auto prompt = reply.isError() ? QString() : reply.argumentAt<1>().path();
        if (prompt.isEmpty() || prompt == "/") {
            refresh();
            return;
        }
        m_bus.connect(service, prompt, "org.freedesktop.Secret.Prompt", "Completed", this, SLOT(refresh()));
        m_bus.asyncCall(QDBusMessage::createMethodCall(service, prompt, "org.freedesktop.Secret.Prompt", "Prompt")
                        << QString());
    });
}

void KeychainService::lock(const QString &path) { call("Lock", {path}); }

void KeychainService::unlock(const QString &path) { call("Unlock", {path}); }

void KeychainService::lockAll()
{
    QStringList paths;
    for (const auto &keychain : std::as_const(m_keychains))
        paths.append(keychain.toMap().value("path").toString());
    if (!paths.isEmpty())
        call("Lock", paths);
}
