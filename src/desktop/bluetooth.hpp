#pragma once
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

// BlueZ's object tree as GetManagedObjects returns it: path -> interface -> properties.
using BluetoothInterfaces = QMap<QString, QVariantMap>;
using BluetoothObjects = QMap<QDBusObjectPath, BluetoothInterfaces>;
Q_DECLARE_METATYPE(BluetoothInterfaces)
Q_DECLARE_METATYPE(BluetoothObjects)

class BluetoothService;

// org.bluez.Agent1, exported on the same connection: BlueZ asks here for PINs, passkey
// confirmations and permission for untrusted devices. Only BlueZ itself may ask; the user's
// answer is sent later, so every question is a delayed reply.
class BluetoothAgent final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
  public:
    explicit BluetoothAgent(BluetoothService *service);

  public slots:
    void Release();
    QString RequestPinCode(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pincode);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void Cancel();

  private:
    bool fromBluez();
    void ask(const QString &kind, const QDBusObjectPath &device, const QString &code = {});
    BluetoothService *m_service;
};

// Bluetooth through BlueZ on the system bus (docs/bluetooth.md). Pairings live in BlueZ, so
// devices paired under KDE are already here. The service follows BlueZ's objects, connects,
// disconnects, pairs and forgets devices, and is the session's default pairing agent. Every
// call is asynchronous: the compositor never waits on the bus. The bus is injectable so tests
// use a fake BlueZ on a private bus. Only the login session asks to be the default agent: a
// desktop running in a window inside KDE leaves device-initiated questions to KDE.
class BluetoothService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(bool agentRegistered READ agentRegistered NOTIFY changed)
    Q_PROPERTY(QString computerName READ computerName NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList nearby READ nearby NOTIFY changed)
    Q_PROPERTY(QVariantMap request READ request NOTIFY requestChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  public:
    explicit BluetoothService(QDBusConnection bus, bool defaultAgent = true, QObject *parent = nullptr);
    ~BluetoothService() override;
    bool available() const { return !m_adapter.isEmpty(); }
    bool powered() const;
    bool discovering() const;
    bool agentRegistered() const { return m_agentRegistered; }
    QString computerName() const;
    // Paired devices by name: {path, name, kind, type, connected, trusted, battery (-1 if
    // unknown), busy ("", "connecting", "disconnecting", "pairing" or "forgetting")}.
    QVariantList devices() const;
    // Named devices found by discovery that are not paired yet: {path, name, kind, type, busy}.
    QVariantList nearby() const;
    // The pairing question on screen: {id, kind, path, name, code, entered}, or empty. Kinds:
    // "pin" and "passkey" (type it), "confirm" (compare the code), "display-pin" and
    // "display-passkey" (type the code on the device), "authorize-pairing", "authorize-service".
    QVariantMap request() const;
    QString message() const { return m_message; }

    Q_INVOKABLE void setPowered(bool on);
    Q_INVOKABLE void connectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    Q_INVOKABLE void forgetDevice(const QString &path);
    // Pairs, then trusts and connects the device, as KDE's Add Device wizard does.
    Q_INVOKABLE void pairDevice(const QString &path);
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    // The user's answer to request(): a PIN or passkey where one is asked for, and "always"
    // to trust a device asking to connect, so it is not asked again.
    Q_INVOKABLE void answer(bool accept, const QString &value = {}, bool always = false);

  signals:
    void changed();
    void requestChanged();
    void messageChanged();
    // A device paired from this session is ready: trusted, and connecting.
    void paired(const QString &path);

  private slots:
    void interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated, const QDBusMessage &message);

  private:
    friend class BluetoothAgent;
    struct Request
    {
        quint64 id = 0;
        QString kind, path, code;
        int entered = -1;
        // The question still waiting for an answer; display requests are answered at once.
        std::optional<QDBusMessage> pending;
    };
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments, int timeout, std::function<void(const QDBusMessage &)> done = {});
    void writeProperty(const QString &path, const QString &interface, const QString &name, const QVariant &value,
                       std::function<void(const QDBusMessage &)> done = {});
    void ownerChanged(const QString &owner);
    void reload();
    void registerAgent();
    void chooseAdapter();
    void scheduleChanged();
    void setMessage(const QString &text);
    void setBusy(const QString &path, const QString &state);
    QString nameOf(const QString &path) const;
    bool onAdapter(const QString &path) const;
    // From the agent: a question (or a code to show) for the device, and BlueZ withdrawing it.
    void agentRequest(const QString &kind, const QString &path, const QString &code, int entered,
                      std::optional<QDBusMessage> pending);
    void agentCanceled();
    void agentReleased();
    void finishRequest();
    QDBusConnection m_bus;
    BluetoothAgent *m_agent = nullptr;
    QString m_owner, m_adapter, m_message, m_pairing;
    QMap<QString, BluetoothInterfaces> m_objects;
    QMap<QString, QString> m_busy;
    std::optional<Request> m_request;
    quint64 m_nextRequest = 0;
    // BlueZ restarts make earlier replies stale.
    quint64 m_generation = 0;
    bool m_defaultAgent, m_agentRegistered = false, m_discoveryRequested = false, m_exported = false;
    QTimer m_changed, m_discoveryLimit;
};
