#include "menu_broker.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QSet>
#include <limits>

namespace
{
QString displayLabel(const QString &label)
{
    QString result;
    for (qsizetype i = 0; i < label.size(); ++i)
    {
        if (label[i] != '_')
        {
            result += label[i];
            continue;
        }
        if (i + 1 < label.size() && label[i + 1] == '_')
        {
            result += '_';
            ++i;
        }
    }
    return result;
}
QVariantMap decode(const QVariant &value, int depth, int &count, int &bytes, bool &valid,
                   QSet<int> &seen)
{
    if (!valid || depth > 16 || ++count > 4096)
    {
        valid = false;
        return {};
    }
    QVariant input = value;
    if (input.metaType() == QMetaType::fromType<QDBusVariant>())
        input = input.value<QDBusVariant>().variant();
    int id = 0;
    QVariantMap properties;
    QVariantList rawChildren;
    if (input.metaType() == QMetaType::fromType<QDBusArgument>())
    {
        const auto argument = input.value<QDBusArgument>();
        if (argument.currentSignature() != "(ia{sv}av)")
        {
            valid = false;
            return {};
        }
        argument.beginStructure();
        argument >> id >> properties;
        argument.beginArray();
        while (!argument.atEnd())
        {
            if (rawChildren.size() >= 4096)
            {
                valid = false;
                return {};
            }
            QDBusVariant child;
            argument >> child;
            rawChildren.push_back(child.variant());
        }
        argument.endArray();
        argument.endStructure();
    }
    else if (input.metaType() == QMetaType::fromType<QVariantMap>())
    {
        const auto map = input.toMap();
        bool ok = false;
        id = map.value("id").toInt(&ok);
        if (!ok || map.value("properties").metaType() != QMetaType::fromType<QVariantMap>() ||
            map.value("children").metaType() != QMetaType::fromType<QVariantList>())
        {
            valid = false;
            return {};
        }
        properties = map.value("properties").toMap();
        rawChildren = map.value("children").toList();
    }
    else
    {
        valid = false;
        return {};
    }
    if (id < 0 || seen.contains(id) || properties.size() > 64)
    {
        valid = false;
        return {};
    }
    seen.insert(id);
    const auto label = properties.value("label").toString();
    const auto length = label.toUtf8().size();
    if (length > 4096 || bytes > 1024 * 1024 - length)
    {
        valid = false;
        return {};
    }
    bytes += int(length);
    QVariantList children;
    for (const auto &raw : rawChildren)
    {
        auto child = decode(raw, depth + 1, count, bytes, valid, seen);
        if (!valid)
            return {};
        children.push_back(child);
    }
    return {{"id", id},
            {"label", displayLabel(label)},
            {"enabled", properties.value("enabled", true).toBool()},
            {"visible", properties.value("visible", true).toBool()},
            {"separator", properties.value("type").toString() == "separator"},
            {"checkable", !properties.value("toggle-type").toString().isEmpty()},
            {"checked", properties.value("toggle-state").toInt() == 1},
            {"children", children}};
}
} // namespace

void MenuRegistrar::RegisterWindow(uint id, QDBusObjectPath path)
{
    emit WindowRegistered(id, message().service(), path);
}
void MenuRegistrar::UnregisterWindow(uint id)
{
    emit WindowUnregistered(id);
}
MenuBroker::MenuBroker(bool enabled, QObject *parent) : QObject(parent), m_busEnabled(enabled)
{
    if (!enabled)
        return;
    auto bus = QDBusConnection::sessionBus();
    m_ownerWatcher = new QDBusServiceWatcher(this);
    m_ownerWatcher->setConnection(bus);
    m_ownerWatcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    connect(m_ownerWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &oldOwner, const QString &owner)
            {
                if (oldOwner.isEmpty() || owner == m_uniqueOwner)
                    return;
                disconnectProvider();
                ++m_generation;
                m_revision = 0;
                m_menus.clear();
                emit changed();
            });
    if (bus.registerService("com.canonical.AppMenu.Registrar"))
    {
        auto *registrar = new MenuRegistrar(this);
        m_registrarOwned =
            bus.registerObject("/com/canonical/AppMenu/Registrar", registrar,
                               QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals);
        if (!m_registrarOwned)
            bus.unregisterService("com.canonical.AppMenu.Registrar");
    }
    else
        qWarning("AppMenu registrar unavailable; native menu export is not established.");
}
MenuBroker::~MenuBroker()
{
    if (!m_registrarOwned)
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject("/com/canonical/AppMenu/Registrar");
    bus.unregisterService("com.canonical.AppMenu.Registrar");
}
void MenuBroker::disconnectProvider()
{
    if (!m_uniqueOwner.isEmpty())
    {
        auto bus = QDBusConnection::sessionBus();
        bus.disconnect(m_uniqueOwner, m_path, "com.canonical.dbusmenu", "LayoutUpdated", this,
                       SLOT(refresh()));
        bus.disconnect(m_uniqueOwner, m_path, "com.canonical.dbusmenu", "ItemsPropertiesUpdated",
                       this, SLOT(refresh()));
    }
    m_uniqueOwner.clear();
    ++m_loadSequence;
    ++m_prepareSequence;
    m_waitingAboutToShow = false;
    m_pendingLoads = 0;
    emit busyChanged();
}
void MenuBroker::select(qulonglong window, QString service, QString path, qint64 pid)
{
    if (window == m_window && service == m_service && path == m_path && pid == m_expectedPid)
        return;
    disconnectProvider();
    m_expectedPid = pid;
    m_window = window;
    m_service = std::move(service);
    m_path = std::move(path);
    m_menus.clear();
    m_revision = 0;
    m_providerRevision = 0;
    if (m_ownerWatcher)
        m_ownerWatcher->setWatchedServices(m_service.isEmpty() ? QStringList{}
                                                               : QStringList{m_service});
    const auto ticket = ++m_generation;
    emit changed();
    if (!m_busEnabled || m_service.isEmpty() || pid <= 0)
        return;
    auto call = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                               "org.freedesktop.DBus", "GetNameOwner");
    call << m_service;
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1000), this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this,
        [this, watcher, ticket, pid]
        {
            auto reply = watcher->reply();
            watcher->deleteLater();
            if (ticket != m_generation || reply.type() == QDBusMessage::ErrorMessage ||
                reply.arguments().size() != 1)
                return;
            const QString owner = reply.arguments().first().toString();
            auto call = QDBusMessage::createMethodCall(
                "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                "GetConnectionUnixProcessID");
            call << owner;
            auto *check = new QDBusPendingCallWatcher(
                QDBusConnection::sessionBus().asyncCall(call, 1000), this);
            connect(
                check, &QDBusPendingCallWatcher::finished, this,
                [this, check, ticket, pid, owner]
                {
                    auto reply = check->reply();
                    check->deleteLater();
                    if (ticket != m_generation || reply.type() == QDBusMessage::ErrorMessage ||
                        reply.arguments().size() != 1)
                        return;
                    if (reply.arguments().first().toUInt() != quint64(pid))
                    {
                        qWarning("Menu endpoint rejected: different process owner");
                        return;
                    }
                    m_uniqueOwner = owner;
                    QDBusConnection::sessionBus().connect(owner, m_path, "com.canonical.dbusmenu",
                                                          "LayoutUpdated", this, SLOT(refresh()));
                    QDBusConnection::sessionBus().connect(owner, m_path, "com.canonical.dbusmenu",
                                                          "ItemsPropertiesUpdated", this,
                                                          SLOT(refresh()));
                    load(ticket);
                });
        });
}
QVariantMap MenuBroker::decodeLayout(const QVariant &value, int depth, int &count, int &bytes,
                                     bool &valid)
{
    QSet<int> seen;
    return decode(value, depth, count, bytes, valid, seen);
}
void MenuBroker::advanceRevision()
{
    if (m_revision == std::numeric_limits<uint>::max())
    {
        // Retire pending work as well as snapshots when the revision wraps.
        ++m_generation;
        ++m_loadSequence;
        ++m_prepareSequence;
        m_waitingAboutToShow = false;
        m_pendingLoads = 0;
        m_revision = 0;
        emit busyChanged();
    }
    ++m_revision;
}
void MenuBroker::setInteractionContext(qulonglong activeWindow, bool blocked)
{
    if (m_contextWindow == activeWindow && m_interactionBlocked == blocked)
        return;
    m_contextWindow = activeWindow;
    m_interactionBlocked = blocked;
    // Keep the authenticated model visible, while retiring actions captured in
    // a different focus/modal context, including those captured before a dialog.
    advanceRevision();
    emit changed();
}
void MenuBroker::load(qulonglong ticket)
{
    if (ticket != m_generation || m_uniqueOwner.isEmpty())
        return;
    const auto sequence = ++m_loadSequence;
    ++m_pendingLoads;
    emit busyChanged();
    auto call = QDBusMessage::createMethodCall(m_uniqueOwner, m_path, "com.canonical.dbusmenu",
                                               "GetLayout");
    call << 0 << -1 << QStringList{"label",       "enabled",      "visible",         "type",
                                   "toggle-type", "toggle-state", "children-display"};
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, ticket, sequence]
            {
                auto reply = watcher->reply();
                watcher->deleteLater();
                if (ticket == m_generation)
                {
                    --m_pendingLoads;
                    emit busyChanged();
                }
                if (ticket != m_generation || sequence != m_loadSequence ||
                    reply.type() == QDBusMessage::ErrorMessage || reply.arguments().size() != 2)
                    return;
                bool valid = true;
                int count = 0, bytes = 0;
                auto root = decodeLayout(reply.arguments()[1], 0, count, bytes, valid);
                if (!valid)
                {
                    m_menus.clear();
                    emit changed();
                    return;
                }
                const auto menus = root.value("children").toList();
                const auto providerRevision = reply.arguments().first().toUInt();
                if (m_revision != 0 && menus == m_menus && providerRevision == m_providerRevision)
                    return; // A no-op refresh must not destroy tracked menu items.
                m_providerRevision = providerRevision;
                // Local snapshot revision also changes for property-only updates, which
                // need not increment the provider's layout revision.
                advanceRevision();
                m_menus = menus;
                emit changed();
            });
}
void MenuBroker::refresh()
{
    load(m_generation);
}
QVariantMap MenuBroker::findItem(const QVariantList &items, int id) const
{
    for (const auto &value : items)
    {
        auto item = value.toMap();
        if (!item.value("visible").toBool() || !item.value("enabled").toBool())
            continue;
        if (item.value("id").toInt() == id)
            return item;
        auto child = findItem(item.value("children").toList(), id);
        if (!child.isEmpty())
            return child;
    }
    return {};
}
void MenuBroker::invoke(int id, qulonglong generation, uint revision)
{
    if (m_interactionBlocked || generation != m_generation || revision != m_revision ||
        m_uniqueOwner.isEmpty())
        return;
    const auto item = findItem(m_menus, id);
    if (item.isEmpty() || !item.value("enabled").toBool() || !item.value("visible").toBool() ||
        item.value("separator").toBool())
        return;
    auto call =
        QDBusMessage::createMethodCall(m_uniqueOwner, m_path, "com.canonical.dbusmenu", "Event");
    call << id << QString("clicked") << QVariant::fromValue(QDBusVariant(0))
         << uint(QDateTime::currentMSecsSinceEpoch());
    // Nothing waits for the answer. send() marks the call no-reply-expected; a call awaiting a
    // reply from an application that never answers holds a slot on the bus until it quits.
    QDBusConnection::sessionBus().send(call);
}
void MenuBroker::prepare(int id)
{
    if (m_interactionBlocked || m_uniqueOwner.isEmpty())
        return;
    m_waitingAboutToShow = true;
    const auto preparation = ++m_prepareSequence;
    emit busyChanged();
    auto call = QDBusMessage::createMethodCall(m_uniqueOwner, m_path, "com.canonical.dbusmenu",
                                               "AboutToShow");
    call << id;
    const auto ticket = m_generation;
    auto *watcher =
        new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, ticket, preparation]
            {
                watcher->deleteLater();
                if (ticket != m_generation || preparation != m_prepareSequence)
                    return;
                m_waitingAboutToShow = false;
                load(ticket);
                emit busyChanged();
            });
}
