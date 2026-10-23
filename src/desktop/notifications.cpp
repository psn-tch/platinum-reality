#include "notifications.hpp"
#include "icon_store.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QTimer>
#include <algorithm>

namespace
{
constexpr int maximumAlerts = 4;
constexpr int defaultTimeout = 6000;

QString bounded(const QString &text, int limit)
{
    auto plain = text.left(limit);
    plain.replace(QChar('\r'), QChar(' '));
    return plain;
}

// The image-data hint: (iiibiiay) width, height, rowstride, has_alpha, bits, channels, data.
QImage imageData(const QVariant &value)
{
    if (!value.canConvert<QDBusArgument>())
        return {};
    const auto argument = value.value<QDBusArgument>();
    int width = 0, height = 0, stride = 0, bits = 0, channels = 0;
    bool alpha = false;
    QByteArray data;
    argument.beginStructure();
    argument >> width >> height >> stride >> alpha >> bits >> channels >> data;
    argument.endStructure();
    if (width < 1 || height < 1 || width > 1024 || height > 1024 || bits != 8 ||
        channels != (alpha ? 4 : 3) || stride < width * channels ||
        data.size() < qsizetype(stride) * (height - 1) + width * channels)
        return {};
    return QImage(reinterpret_cast<const uchar *>(data.constData()), width, height, stride,
                  alpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888)
        .copy();
}
} // namespace

NotificationServer::NotificationServer(bool connectBus, std::shared_ptr<IconStore> icons, QObject *parent)
    : QObject(parent), m_icons(std::move(icons))
{
    if (!connectBus)
        return;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.registerService("org.freedesktop.Notifications"))
        return;
    m_registered = bus.registerObject("/org/freedesktop/Notifications", this,
                                      QDBusConnection::ExportScriptableSlots |
                                          QDBusConnection::ExportScriptableSignals);
    if (!m_registered)
        bus.unregisterService("org.freedesktop.Notifications");
}

NotificationServer::~NotificationServer()
{
    if (!m_registered)
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject("/org/freedesktop/Notifications");
    bus.unregisterService("org.freedesktop.Notifications");
}

QVariantList NotificationServer::alerts() const
{
    QList<std::pair<quint64, uint>> order;
    for (auto it = m_alerts.cbegin(); it != m_alerts.cend(); ++it)
        order.append({it->order, it.key()});
    std::sort(order.begin(), order.end());
    QVariantList result;
    for (const auto &[_, id] : order)
    {
        const auto &alert = m_alerts[id];
        const bool hasDefault = std::any_of(alert.actions.cbegin(), alert.actions.cend(), [](const QVariant &a)
                                            { return a.toMap().value("key") == "default"; });
        QVariantList buttons;
        for (const auto &action : alert.actions)
            if (action.toMap().value("key") != "default")
                buttons.append(action);
        result.append(QVariantMap{{"id", id},
                                  {"application", alert.application},
                                  {"summary", alert.summary},
                                  {"body", alert.body},
                                  {"icon", alert.icon},
                                  {"actions", buttons},
                                  {"hasDefault", hasDefault}});
    }
    return result;
}

QStringList NotificationServer::GetCapabilities()
{
    return {"actions", "body", "icon-static", "persistence"};
}

QString NotificationServer::GetServerInformation(QString &vendor, QString &version, QString &spec_version)
{
    vendor = "Platinum Reality";
    version = "0.1.0";
    spec_version = "1.2";
    return "Platinum Reality Notification Manager";
}

QString NotificationServer::imageFor(uint id, const QString &icon, const QVariantMap &hints)
{
    // Priority from the specification: image-data, image-path, app_icon, then icon_data.
    QImage image;
    for (const auto *key : {"image-data", "image_data"})
        if (image.isNull() && hints.contains(key))
            image = imageData(hints.value(key));
    for (const auto *key : {"image-path", "image_path"})
        if (image.isNull() && hints.contains(key))
            image = IconStore::resolve(hints.value(key).toString(), 64);
    if (image.isNull())
        image = IconStore::resolve(icon, 64);
    if (image.isNull() && hints.contains("icon_data"))
        image = imageData(hints.value("icon_data"));
    const auto key = "notification/" + QString::number(id);
    if (image.isNull())
    {
        m_icons->remove(key);
        return {};
    }
    m_icons->set(key, image.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    return "image://platinum/" + key + "@" + QString::number(++m_imageRevision);
}

uint NotificationServer::Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                                const QString &summary, const QString &body, const QStringList &actions,
                                const QVariantMap &hints, int expire_timeout)
{
    uint id = replaces_id && m_alerts.contains(replaces_id) ? replaces_id : 0;
    if (!id)
    {
        do
            id = ++m_nextId;
        while (!id || m_alerts.contains(id));
        while (m_alerts.size() >= maximumAlerts)
        {
            const auto oldest = std::min_element(m_alerts.cbegin(), m_alerts.cend(),
                [](const Alert &a, const Alert &b) { return a.order < b.order; });
            close(oldest.key(), Expired);
        }
    }
    auto &alert = m_alerts[id];
    alert.application = bounded(app_name, 64);
    alert.summary = bounded(summary, 200);
    alert.body = bounded(body, 1000);
    alert.actions.clear();
    for (qsizetype i = 0; i + 1 < actions.size() && alert.actions.size() < 6; i += 2)
        alert.actions.append(QVariantMap{{"key", actions[i].left(64)}, {"label", bounded(actions[i + 1], 40)}});
    alert.resident = hints.value("resident").toBool();
    alert.icon = imageFor(id, app_icon, hints);
    if (!alert.order)
        alert.order = ++m_nextOrder;
    // Critical notifications stay until dismissed unless the sender gave a timeout.
    const bool critical = hints.value("urgency").toUInt() == 2;
    const int timeout = expire_timeout < 0 ? (critical ? 0 : defaultTimeout) : expire_timeout;
    if (!alert.timer)
    {
        alert.timer = new QTimer(this);
        alert.timer->setSingleShot(true);
        connect(alert.timer, &QTimer::timeout, this, [this, id] { close(id, Expired); });
    }
    alert.timer->stop();
    if (timeout > 0)
        alert.timer->start(timeout);
    emit alertsChanged();
    return id;
}

void NotificationServer::CloseNotification(uint id)
{
    close(id, Closed);
}

void NotificationServer::dismiss(uint id)
{
    close(id, Dismissed);
}

void NotificationServer::invoke(uint id, const QString &action)
{
    const auto it = m_alerts.constFind(id);
    if (it == m_alerts.cend() ||
        std::none_of(it->actions.cbegin(), it->actions.cend(),
                     [&](const QVariant &a) { return a.toMap().value("key") == action; }))
        return;
    if (m_activationTokens)
        emit ActivationToken(id, m_activationTokens());
    emit ActionInvoked(id, action);
    if (!it->resident)
        close(id, Dismissed);
}

void NotificationServer::close(uint id, Reason reason)
{
    const auto it = m_alerts.find(id);
    if (it == m_alerts.end())
        return;
    if (it->timer)
        it->timer->deleteLater();
    m_alerts.erase(it);
    m_icons->remove("notification/" + QString::number(id));
    emit NotificationClosed(id, reason);
    emit alertsChanged();
}
