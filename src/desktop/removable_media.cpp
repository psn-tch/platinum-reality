#include "removable_media.hpp"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QFileInfo>

namespace
{
const auto service = QStringLiteral("org.freedesktop.UDisks2");
const auto root = QStringLiteral("/org/freedesktop/UDisks2");
const auto blockInterface = QStringLiteral("org.freedesktop.UDisks2.Block");
const auto filesystemInterface = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const auto driveInterface = QStringLiteral("org.freedesktop.UDisks2.Drive");

// Filesystem.MountPoints is aay: NUL-terminated paths. Inside a variant it arrives unconverted.
QStringList mountPoints(const QVariant &value)
{
    QList<QByteArray> raw;
    if (value.canConvert<QDBusArgument>())
        value.value<QDBusArgument>() >> raw;
    else
        raw = value.value<QList<QByteArray>>();
    QStringList result;
    for (auto path : raw)
    {
        if (path.endsWith('\0'))
            path.chop(1);
        if (!path.isEmpty())
            result << QString::fromLocal8Bit(path);
    }
    return result;
}
QString objectPath(const QVariant &value)
{
    return value.canConvert<QDBusObjectPath>() ? value.value<QDBusObjectPath>().path() : value.toString();
}
} // namespace

RemovableMedia::RemovableMedia(QDBusConnection bus, bool automount, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_automount(automount)
{
    qDBusRegisterMetaType<BluetoothInterfaces>();
    qDBusRegisterMetaType<BluetoothObjects>();
    if (!m_bus.isConnected())
        return;
    m_bus.connect(service, QString(), "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
                  SLOT(interfacesAdded(QDBusObjectPath, BluetoothInterfaces)));
    m_bus.connect(service, QString(), "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this,
                  SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
    m_bus.connect(service, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    // Starts UDisks2 if it is not running yet (it is D-Bus activated).
    call(root, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", [this](const QDBusMessage &reply)
    {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return;
        const auto objects = qdbus_cast<BluetoothObjects>(reply.arguments().first());
        for (auto it = objects.cbegin(); it != objects.cend(); ++it)
            m_objects[it.key().path()].insert(it.value());
        // A disk left in at login is put on the desktop too, as at a Mac's startup.
        for (const auto &path : m_objects.keys())
            consider(path);
    }, false);
}

void RemovableMedia::call(const QString &path, const QString &interface, const QString &method,
                          std::function<void(const QDBusMessage &)> done, bool options)
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    if (options)
        message << QVariantMap{};
    // Mounting checks the filesystem first; a large or slow disk can take a while.
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, done = std::move(done)]
    {
        watcher->deleteLater();
        done(watcher->reply());
    });
}

bool RemovableMedia::removable(const QString &path) const
{
    const auto drive = objectPath(m_objects.value(path).value(blockInterface).value("Drive"));
    const auto properties = m_objects.value(drive).value(driveInterface);
    return properties.value("Removable").toBool() || properties.value("MediaRemovable").toBool();
}

void RemovableMedia::consider(const QString &path)
{
    const auto &interfaces = m_objects.value(path);
    if (!m_automount || m_tried.contains(path) || !interfaces.contains(filesystemInterface))
        return;
    const auto block = interfaces.value(blockInterface);
    if (!block.value("HintAuto").toBool() || block.value("HintIgnore").toBool() ||
        block.value("HintSystem").toBool() || block.value("IdUsage").toString() != "filesystem" ||
        !mountPoints(interfaces.value(filesystemInterface).value("MountPoints")).isEmpty() || !removable(path))
        return;
    m_tried.insert(path);
    call(path, filesystemInterface, "Mount", [this](const QDBusMessage &reply)
    {
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
            emit mounted(reply.arguments().first().toString());
        else
            qWarning("Removable media: mount: %s", qPrintable(reply.errorMessage()));
    });
}

void RemovableMedia::interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces)
{
    m_objects[path.path()].insert(interfaces);
    // A drive can be announced after its filesystem: look at every candidate again.
    for (const auto &candidate : m_objects.keys())
        consider(candidate);
}

void RemovableMedia::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    auto object = m_objects.find(path.path());
    if (object == m_objects.end())
        return;
    for (const auto &interface : interfaces)
        object->remove(interface);
    if (!object->contains(filesystemInterface))
        m_tried.remove(path.path());
    if (object->isEmpty())
        m_objects.erase(object);
}

void RemovableMedia::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                       const QStringList &invalidated, const QDBusMessage &message)
{
    auto object = m_objects.find(message.path());
    if (object == m_objects.end() || !object->contains(interface))
        return;
    auto &properties = (*object)[interface];
    for (auto it = changed.cbegin(); it != changed.cend(); ++it)
        properties.insert(it.key(), it.value());
    for (const auto &name : invalidated)
        properties.remove(name);
}

QString RemovableMedia::filesystemAt(const QString &mountPoint) const
{
    const auto wanted = QFileInfo(mountPoint).absoluteFilePath();
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
        if (mountPoints(it.value().value(filesystemInterface).value("MountPoints")).contains(wanted))
            return it.key();
    return {};
}

bool RemovableMedia::canEject(const QString &mountPoint) const
{
    const auto path = filesystemAt(mountPoint);
    return !path.isEmpty() && removable(path);
}

bool RemovableMedia::eject(const QString &mountPoint)
{
    const auto path = filesystemAt(mountPoint);
    if (path.isEmpty() || !removable(path))
        return false;
    const auto name = QFileInfo(mountPoint).fileName();
    const auto drive = objectPath(m_objects.value(path).value(blockInterface).value("Drive"));
    call(path, filesystemInterface, "Unmount", [this, mountPoint, name, drive](const QDBusMessage &reply)
    {
        if (reply.type() != QDBusMessage::ReplyMessage)
        {
            emit failed(reply.errorName() == "org.freedesktop.UDisks2.Error.DeviceBusy"
                            ? QString("The disk “%1” could not be put away, because it contains items that are in use.").arg(name)
                            : QString("The disk “%1” could not be put away. %2").arg(name, reply.errorMessage()));
            return;
        }
        // Unmounted: safe to pull out. Power the stick off too, where the drive allows it.
        const auto properties = m_objects.value(drive).value(driveInterface);
        if (properties.value("CanPowerOff").toBool())
            call(drive, driveInterface, "PowerOff", [](const QDBusMessage &) {});
        else if (properties.value("Ejectable").toBool())
            call(drive, driveInterface, "Eject", [](const QDBusMessage &) {});
        emit ejected(mountPoint);
    });
    return true;
}
