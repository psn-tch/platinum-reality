#include "status_items.hpp"
#include "icon_store.hpp"
#include "menu_broker.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>
#include <QtEndian>

namespace
{
constexpr int maximumItems = 32;
constexpr int iconSize = 40; // 20 px modules at up to 2x.
const QString itemInterface = "org.kde.StatusNotifierItem";
const QString menuInterface = "com.canonical.dbusmenu";

bool validPath(const QString &path)
{
    static const QRegularExpression pattern("^/(?:[A-Za-z0-9_]+(?:/[A-Za-z0-9_]+)*)?$");
    return path.size() <= 255 && pattern.match(path).hasMatch();
}

// IconPixmap: a(iiay), ARGB32 in network byte order. Picks the size nearest the module's.
QImage pixmapIcon(const QVariant &value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    const auto argument = value.value<QDBusArgument>();
    QImage best;
    argument.beginArray();
    for (int count = 0; !argument.atEnd() && count < 16; ++count)
    {
        int width = 0, height = 0;
        QByteArray data;
        argument.beginStructure();
        argument >> width >> height >> data;
        argument.endStructure();
        if (width < 1 || height < 1 || width > 512 || height > 512 || data.size() != qsizetype(width) * height * 4)
            continue;
        QImage image(width, height, QImage::Format_ARGB32);
        for (int y = 0; y < height; ++y)
        {
            auto *row = reinterpret_cast<quint32 *>(image.scanLine(y));
            for (int x = 0; x < width; ++x)
                row[x] = qFromBigEndian<quint32>(data.constData() + (qsizetype(y) * width + x) * 4);
        }
        const auto distance = [](const QImage &i) { return std::abs(i.width() - iconSize); };
        if (best.isNull() || distance(image) < distance(best))
            best = image;
    }
    argument.endArray();
    return best;
}

QVariantMap findEntry(const QVariantList &entries, int id)
{
    for (const auto &value : entries)
    {
        const auto entry = value.toMap();
        if (entry.value("id").toInt() == id)
            return entry;
        if (auto child = findEntry(entry.value("children").toList(), id); !child.isEmpty())
            return child;
    }
    return {};
}
} // namespace

StatusWatcherAdaptor::StatusWatcherAdaptor(StatusItems *items) : QObject(items), m_items(items)
{
    connect(items, &StatusItems::itemAdded, this, &StatusWatcherAdaptor::StatusNotifierItemRegistered);
    connect(items, &StatusItems::itemRemoved, this, &StatusWatcherAdaptor::StatusNotifierItemUnregistered);
}

QStringList StatusWatcherAdaptor::registeredItems() const
{
    return m_items->keys();
}

void StatusWatcherAdaptor::RegisterStatusNotifierItem(const QString &serviceOrPath)
{
    if (m_items->add(message().service(), serviceOrPath).isEmpty() && calledFromDBus())
        sendErrorReply(QDBusError::InvalidArgs, "The status item could not be registered");
}

void StatusWatcherAdaptor::RegisterStatusNotifierHost(const QString &)
{
    // The desktop is the only host; nothing to record.
}

StatusItems::StatusItems(bool connectBus, std::shared_ptr<IconStore> icons, QObject *parent)
    : QObject(parent), m_icons(std::move(icons))
{
    if (!connectBus)
        return;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    m_adaptor = new StatusWatcherAdaptor(this);
    if (!bus.registerObject("/StatusNotifierWatcher", m_adaptor,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals |
                                QDBusConnection::ExportAllProperties))
        return;
    if (!bus.registerService("org.kde.StatusNotifierWatcher"))
    {
        bus.unregisterObject("/StatusNotifierWatcher");
        return;
    }
    m_registered = true;
    m_watcher = new QDBusServiceWatcher(this);
    m_watcher->setConnection(bus);
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this,
            [this](const QString &service)
            {
                m_watcher->removeWatchedService(service);
                for (const auto &key : QStringList(m_order))
                    if (m_items.value(key).service == service)
                        remove(key);
            });
    emit m_adaptor->StatusNotifierHostRegistered();
}

StatusItems::~StatusItems()
{
    if (!m_registered)
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterService("org.kde.StatusNotifierWatcher");
    bus.unregisterObject("/StatusNotifierWatcher");
}

QString StatusItems::keyFor(const QString &service, const QString &path) const
{
    return service + path;
}

QString StatusItems::add(const QString &sender, const QString &serviceOrPath)
{
    // Ayatana/libappindicator pass their object path; KDE clients pass a bus name.
    QString service = serviceOrPath.startsWith('/') ? sender : serviceOrPath;
    const QString path = serviceOrPath.startsWith('/') ? serviceOrPath : QString("/StatusNotifierItem");
    if (service.isEmpty() || service.size() > 255 || !validPath(path))
        return {};
    auto bus = QDBusConnection::sessionBus();
    // Signals arrive from the unique name, so key items by it.
    if (!service.startsWith(':'))
    {
        const auto owner = bus.interface()->serviceOwner(service);
        if (!owner.isValid())
            return {};
        service = owner.value();
    }
    const auto key = keyFor(service, path);
    if (m_items.contains(key))
    {
        fetch(key);
        return key;
    }
    if (m_items.size() >= maximumItems)
        return {};
    Item item;
    item.service = service;
    item.path = path;
    item.serial = ++m_nextSerial;
    item.refresh = new QTimer(this);
    item.refresh->setSingleShot(true);
    item.refresh->setInterval(30);
    connect(item.refresh, &QTimer::timeout, this, [this, key] { fetch(key); });
    m_items.insert(key, item);
    m_order.append(key);
    m_watcher->addWatchedService(service);
    for (const auto *signal : {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewToolTip", "NewStatus"})
        bus.connect(service, path, itemInterface, signal, this, SLOT(itemSignal(QDBusMessage)));
    fetch(key);
    emit itemAdded(key);
    return key;
}

void StatusItems::remove(const QString &key)
{
    const auto it = m_items.find(key);
    if (it == m_items.end())
        return;
    auto bus = QDBusConnection::sessionBus();
    for (const auto *signal : {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewToolTip", "NewStatus"})
        bus.disconnect(it->service, it->path, itemInterface, signal, this, SLOT(itemSignal(QDBusMessage)));
    if (!it->menuPath.isEmpty())
        for (const auto *signal : {"LayoutUpdated", "ItemsPropertiesUpdated"})
            bus.disconnect(it->service, it->menuPath, menuInterface, signal, this, SLOT(menuSignal(QDBusMessage)));
    it->refresh->deleteLater();
    m_icons->remove("status/" + QString::number(it->serial));
    m_items.erase(it);
    m_order.removeAll(key);
    emit itemRemoved(key);
    emit itemsChanged();
}

void StatusItems::fetch(const QString &key)
{
    const auto it = m_items.constFind(key);
    if (it == m_items.cend())
        return;
    auto call = QDBusMessage::createMethodCall(it->service, it->path, "org.freedesktop.DBus.Properties", "GetAll");
    call << itemInterface;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 2000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, key]
            {
                const auto reply = watcher->reply();
                watcher->deleteLater();
                auto it = m_items.find(key);
                if (it == m_items.end() || reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1)
                    return;
                const auto properties = qdbus_cast<QVariantMap>(reply.arguments().first());
                it->id = properties.value("Id").toString().left(64);
                it->title = properties.value("Title").toString().left(64);
                it->status = properties.value("Status").toString().left(32);
                it->itemIsMenu = properties.value("ItemIsMenu").toBool();
                const auto themePath = properties.value("IconThemePath").toString();
                const bool attention = it->status == "NeedsAttention";
                QImage image;
                if (attention)
                {
                    image = IconStore::resolve(properties.value("AttentionIconName").toString(), iconSize, {themePath});
                    if (image.isNull())
                        image = pixmapIcon(properties.value("AttentionIconPixmap"));
                }
                if (image.isNull())
                    image = IconStore::resolve(properties.value("IconName").toString(), iconSize, {themePath});
                if (image.isNull())
                    image = pixmapIcon(properties.value("IconPixmap"));
                const auto iconKey = "status/" + QString::number(it->serial);
                if (image.isNull())
                {
                    m_icons->remove(iconKey);
                    it->icon.clear();
                }
                else
                {
                    m_icons->set(iconKey, image);
                    it->icon = "image://platinum/" + iconKey + "@" + QString::number(++m_iconRevision);
                }
                const auto menuPath = properties.value("Menu").value<QDBusObjectPath>().path();
                if (menuPath != it->menuPath && (menuPath.isEmpty() || validPath(menuPath)))
                {
                    auto bus = QDBusConnection::sessionBus();
                    for (const auto *signal : {"LayoutUpdated", "ItemsPropertiesUpdated"})
                    {
                        if (!it->menuPath.isEmpty())
                            bus.disconnect(it->service, it->menuPath, menuInterface, signal, this,
                                           SLOT(menuSignal(QDBusMessage)));
                        if (!menuPath.isEmpty())
                            bus.connect(it->service, menuPath, menuInterface, signal, this,
                                        SLOT(menuSignal(QDBusMessage)));
                    }
                    it->menuPath = menuPath;
                    it->menu.clear();
                    loadMenu(key);
                }
                emit itemsChanged();
            });
}

void StatusItems::loadMenu(const QString &key)
{
    const auto it = m_items.constFind(key);
    if (it == m_items.cend() || it->menuPath.isEmpty())
        return;
    auto call = QDBusMessage::createMethodCall(it->service, it->menuPath, menuInterface, "GetLayout");
    call << 0 << -1
         << QStringList{"label", "enabled", "visible", "type", "toggle-type", "toggle-state", "children-display"};
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 2000), this);
    const auto menuPath = it->menuPath;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, key, menuPath]
            {
                const auto reply = watcher->reply();
                watcher->deleteLater();
                auto it = m_items.find(key);
                if (it == m_items.end() || it->menuPath != menuPath ||
                    reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 2)
                    return;
                bool valid = true;
                int count = 0, bytes = 0;
                const auto root = MenuBroker::decodeLayout(reply.arguments()[1], 0, count, bytes, valid);
                it->menu = valid ? root.value("children").toList() : QVariantList{};
                ++m_menuRevision;
                emit menusChanged();
            });
}

QVariantList StatusItems::items() const
{
    QVariantList result;
    for (const auto &key : m_order)
    {
        const auto &item = m_items[key];
        if (item.status == "Passive" || item.status.isEmpty())
            continue;
        result.append(QVariantMap{{"key", key},
                                  {"id", item.id},
                                  {"title", item.title.isEmpty() ? item.id : item.title},
                                  {"icon", item.icon},
                                  {"status", item.status},
                                  {"itemIsMenu", item.itemIsMenu},
                                  {"hasMenu", !item.menu.isEmpty()}});
    }
    return result;
}

QVariantList StatusItems::menu(const QString &key) const
{
    return m_items.value(key).menu;
}

void StatusItems::prepare(const QString &key)
{
    const auto it = m_items.constFind(key);
    if (it == m_items.cend() || it->menuPath.isEmpty())
        return;
    auto call = QDBusMessage::createMethodCall(it->service, it->menuPath, menuInterface, "AboutToShow");
    call << 0;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 1000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, key]
            {
                watcher->deleteLater();
                loadMenu(key);
            });
}

void StatusItems::activate(const QString &key)
{
    const auto it = m_items.constFind(key);
    if (it == m_items.cend())
        return;
    auto call = QDBusMessage::createMethodCall(it->service, it->path, itemInterface, "Activate");
    call << 0 << 0;
    // Nothing waits for the answer; see MenuBroker::invoke.
    QDBusConnection::sessionBus().send(call);
}

void StatusItems::invoke(const QString &key, int id)
{
    const auto it = m_items.constFind(key);
    if (it == m_items.cend() || it->menuPath.isEmpty())
        return;
    const auto entry = findEntry(it->menu, id);
    if (entry.isEmpty() || !entry.value("enabled").toBool() || !entry.value("visible").toBool() ||
        entry.value("separator").toBool())
        return;
    auto call = QDBusMessage::createMethodCall(it->service, it->menuPath, menuInterface, "Event");
    call << id << QString("clicked") << QVariant::fromValue(QDBusVariant(0))
         << uint(QDateTime::currentMSecsSinceEpoch());
    QDBusConnection::sessionBus().send(call);
}

void StatusItems::itemSignal(const QDBusMessage &message)
{
    const auto it = m_items.find(keyFor(message.service(), message.path()));
    if (it != m_items.end())
        it->refresh->start();
}

void StatusItems::menuSignal(const QDBusMessage &message)
{
    for (const auto &key : m_order)
    {
        const auto &item = m_items[key];
        if (item.service == message.service() && item.menuPath == message.path())
            loadMenu(key);
    }
}
