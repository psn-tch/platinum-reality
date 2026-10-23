#pragma once
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantList>
class QDBusServiceWatcher;

// Isolated-session registrar: announces availability to native Qt exporters.
// Native Wayland association still comes from the surface protocol, never XID
// guessing. Xwayland registrar mapping is not implemented in this backend.
class MenuRegistrar final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.AppMenu.Registrar")
  public:
    explicit MenuRegistrar(QObject *parent = nullptr) : QObject(parent) {}
  public slots:
    void RegisterWindow(uint id, QDBusObjectPath path);
    void UnregisterWindow(uint id);
  signals:
    void WindowRegistered(uint id, QString service, QDBusObjectPath path);
    void WindowUnregistered(uint id);
};

class MenuBroker final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList menus READ menus NOTIFY changed)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY changed)
    Q_PROPERTY(uint revision READ revision NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool interactionBlocked READ interactionBlocked NOTIFY changed)
  public:
    explicit MenuBroker(bool connectBus, QObject *parent = nullptr);
    ~MenuBroker() override;
    QVariantList menus() const
    {
        return m_menus;
    }
    qulonglong generation() const
    {
        return m_generation;
    }
    uint revision() const
    {
        return m_revision;
    }
    bool busy() const
    {
        return m_waitingAboutToShow || m_pendingLoads > 0;
    }
    void select(qulonglong window, QString service, QString path, qint64 expectedPid);
    bool interactionBlocked() const { return m_interactionBlocked; }
    void setInteractionContext(qulonglong activeWindow, bool blocked);
    Q_INVOKABLE void invoke(int item, qulonglong generation, uint revision);
    Q_INVOKABLE void prepare(int item);
    static QVariantMap decodeLayout(const QVariant &value, int depth, int &count, int &textBytes,
                                    bool &valid);
  signals:
    void changed();
    void busyChanged();
  private slots:
    void refresh();

  private:
    void load(qulonglong generation);
    void disconnectProvider();
    void advanceRevision();
    QVariantMap findItem(const QVariantList &items, int id) const;
    QVariantList m_menus;
    qulonglong m_generation = 0, m_window = 0;
    qulonglong m_loadSequence = 0;
    qulonglong m_prepareSequence = 0;
    int m_pendingLoads = 0;
    bool m_waitingAboutToShow = false;
    bool m_interactionBlocked = false;
    qulonglong m_contextWindow = 0;
    qint64 m_expectedPid = 0;
    uint m_revision = 0;
    uint m_providerRevision = 0;
    QString m_service, m_path, m_uniqueOwner;
    bool m_busEnabled = false;
    bool m_registrarOwned = false;
    QDBusServiceWatcher *m_ownerWatcher = nullptr;
};
