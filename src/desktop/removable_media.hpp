#pragma once
#include "bluetooth.hpp" // BluetoothInterfaces/BluetoothObjects: any ObjectManager's tree
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QSet>
#include <functional>

// Flash drives and other removable disks through UDisks2 on the system bus. In the whole session
// (automount), a removable disk's filesystem is mounted when it appears, as Mac OS 9 put every
// inserted disk on the desktop; the desktop shows mounted drives itself. Eject unmounts a disk the
// user chose and powers its drive off where it can. Only removable drives that UDisks2 marks for
// automatic handling are ever mounted: never system disks, ignored or encrypted ones. polkit lets
// an active local session do both. Every call is asynchronous; the bus is injectable for tests.
class RemovableMedia final : public QObject
{
    Q_OBJECT
  public:
    RemovableMedia(QDBusConnection bus, bool automount, QObject *parent = nullptr);
    bool available() const
    {
        return m_bus.isConnected();
    }
    // A removable disk mounted at this folder, which Eject can put away.
    Q_INVOKABLE bool canEject(const QString &mountPoint) const;
    Q_INVOKABLE bool eject(const QString &mountPoint);

  signals:
    void mounted(QString mountPoint);
    void ejected(QString mountPoint);
    void failed(QString message);

  private slots:
    void interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void propertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated,
                           const QDBusMessage &message);

  private:
    void consider(const QString &path);
    bool removable(const QString &path) const;
    QString filesystemAt(const QString &mountPoint) const;
    // UDisks2 methods take an options dictionary (sent empty); GetManagedObjects takes nothing.
    void call(const QString &path, const QString &interface, const QString &method,
              std::function<void(const QDBusMessage &)> done, bool options = true);
    QDBusConnection m_bus;
    bool m_automount;
    QMap<QString, BluetoothInterfaces> m_objects;
    QSet<QString> m_tried; // mounted once per appearance: a disk the user ejected stays put away
};
