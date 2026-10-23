#pragma once
#include "bluetooth.hpp" // The object tree types: BlueZ and iwd both use the D-Bus object manager.
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <optional>

class WifiService;

// net.connman.iwd.Agent, exported on the same connection: iwd asks here for the password of a
// network being joined. Only iwd may ask; the user's answer is sent later (a delayed reply).
class WifiAgent final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.connman.iwd.Agent")
  public:
    explicit WifiAgent(WifiService *service);

  public slots:
    void Release();
    QString RequestPassphrase(const QDBusObjectPath &network);
    QString RequestPrivateKeyPassphrase(const QDBusObjectPath &network);
    QString RequestUserNameAndPassword(const QDBusObjectPath &network, QString &password);
    QString RequestUserPassword(const QDBusObjectPath &network, const QString &user);
    void Cancel(const QString &reason);

  private:
    bool fromIwd();
    void ask(const QString &kind, const QDBusObjectPath &network, const QString &user = {});
    WifiService *m_service;
};

// Wi-Fi networks through iwd on the system bus (docs/wifi.md), as impala does: the networks the
// first station sees, strongest first, joining (asking for a password through the agent),
// leaving, forgetting and scanning. Turning Wi-Fi on and off stays with SystemControls. Every
// call is asynchronous: the compositor never waits on the bus. The bus is injectable so tests
// use a fake iwd on a private bus.
class WifiService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(bool agentRegistered READ agentRegistered NOTIFY changed)
    Q_PROPERTY(QVariantList networks READ networks NOTIFY changed)
    Q_PROPERTY(QVariantMap request READ request NOTIFY requestChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  public:
    explicit WifiService(QDBusConnection bus, QObject *parent = nullptr);
    ~WifiService() override;
    // A station (a Wi-Fi device in station mode, powered) exists.
    bool available() const { return !m_station.isEmpty(); }
    bool scanning() const;
    bool agentRegistered() const { return m_agentRegistered; }
    // Strongest first: {path, name, type ("open", "psk", "8021x", ...), secure, bars (1 to 4),
    // connected, known, busy ("" or "joining")}.
    QVariantList networks() const;
    // The password question on screen: {id, kind, path, name, type, user}, or empty. Kinds:
    // "passphrase", "private-key", "user-password" (a user name and a password), "password".
    QVariantMap request() const;
    QString message() const { return m_message; }

    Q_INVOKABLE void connectNetwork(const QString &path);
    Q_INVOKABLE void disconnectNetwork();
    Q_INVOKABLE void forgetNetwork(const QString &path);
    Q_INVOKABLE void scan();
    // The user's answer to request(): the password, and the user name where one is asked for.
    Q_INVOKABLE void answer(bool accept, const QString &password = {}, const QString &user = {});
    Q_INVOKABLE void clearMessage() { setMessage({}); }

  signals:
    void changed();
    void requestChanged();
    void messageChanged();

  private slots:
    void interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated, const QDBusMessage &message);

  private:
    friend class WifiAgent;
    struct Request
    {
        quint64 id = 0;
        QString kind, path, user;
        QDBusMessage pending;
    };
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments, int timeout, std::function<void(const QDBusMessage &)> done = {});
    void ownerChanged(const QString &owner);
    void reload();
    void registerAgent();
    void chooseStation();
    void refreshNetworks();
    void scheduleChanged();
    void setMessage(const QString &text);
    QString nameOf(const QString &path) const;
    void agentRequest(const QString &kind, const QString &path, const QString &user, const QDBusMessage &pending);
    void agentCanceled();
    void agentReleased();
    void finishRequest();
    QDBusConnection m_bus;
    WifiAgent *m_agent = nullptr;
    QString m_owner, m_station, m_message, m_joining;
    QMap<QString, BluetoothInterfaces> m_objects;
    // From the station's GetOrderedNetworks: network path and signal strength (100 * dBm).
    QList<std::pair<QString, int>> m_ordered;
    std::optional<Request> m_request;
    quint64 m_nextRequest = 0;
    // iwd restarts make earlier replies stale.
    quint64 m_generation = 0;
    bool m_agentRegistered = false, m_exported = false;
    QTimer m_changed, m_refresh;
};
