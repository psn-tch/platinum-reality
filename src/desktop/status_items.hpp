#pragma once
#include <QDBusContext>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <memory>

class IconStore;
class QDBusMessage;
class QDBusServiceWatcher;
class QTimer;

// org.kde.StatusNotifierWatcher on the session's private bus. Registered tray items (Steam,
// blueman, chat apps) become Control Strip modules whose pop-up menu is the item's own
// com.canonical.dbusmenu menu (docs/tray-and-notifications.md).
class StatusWatcherAdaptor final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)
  public:
    explicit StatusWatcherAdaptor(class StatusItems *items);
    QStringList registeredItems() const;
    bool hostRegistered() const
    {
        return true;
    }
    int protocolVersion() const
    {
        return 0;
    }
  public slots:
    void RegisterStatusNotifierItem(const QString &serviceOrPath);
    void RegisterStatusNotifierHost(const QString &service);
  signals:
    void StatusNotifierItemRegistered(const QString &item);
    void StatusNotifierItemUnregistered(const QString &item);
    void StatusNotifierHostRegistered();
    void StatusNotifierHostUnregistered();

  private:
    StatusItems *m_items;
};

class StatusItems final : public QObject
{
    Q_OBJECT
    // {key, id, title, icon, status, itemIsMenu, hasMenu}, in registration order; Passive hidden.
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(uint menuRevision READ menuRevision NOTIFY menusChanged)
  public:
    StatusItems(bool connectBus, std::shared_ptr<IconStore> icons, QObject *parent = nullptr);
    ~StatusItems() override;
    bool registered() const
    {
        return m_registered;
    }
    QVariantList items() const;
    uint menuRevision() const
    {
        return m_menuRevision;
    }
    QStringList keys() const
    {
        return m_order;
    }
    // Decoded menu entries ({id, label, enabled, visible, separator, checkable, checked, children}).
    Q_INVOKABLE QVariantList menu(const QString &key) const;
    // Before showing the menu: AboutToShow, then a fresh layout.
    Q_INVOKABLE void prepare(const QString &key);
    Q_INVOKABLE void activate(const QString &key);
    Q_INVOKABLE void invoke(const QString &key, int id);
    // Registers an item for a sender; returns the item key, or empty when refused.
    QString add(const QString &sender, const QString &serviceOrPath);

  signals:
    void itemsChanged();
    void menusChanged();
    void itemAdded(const QString &key);
    void itemRemoved(const QString &key);

  private slots:
    void itemSignal(const QDBusMessage &message);
    void menuSignal(const QDBusMessage &message);

  private:
    struct Item
    {
        QString service, path, menuPath, id, title, status, icon;
        quint64 serial = 0;
        bool itemIsMenu = false;
        QVariantList menu;
        QTimer *refresh = nullptr;
    };
    void fetch(const QString &key);
    void loadMenu(const QString &key);
    void remove(const QString &key);
    QString keyFor(const QString &service, const QString &path) const;
    std::shared_ptr<IconStore> m_icons;
    QHash<QString, Item> m_items;
    QStringList m_order;
    QDBusServiceWatcher *m_watcher = nullptr;
    StatusWatcherAdaptor *m_adaptor = nullptr;
    quint64 m_iconRevision = 0;
    quint64 m_nextSerial = 0;
    uint m_menuRevision = 0;
    bool m_registered = false;
};
