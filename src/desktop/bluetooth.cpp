#include "bluetooth.hpp"
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QSet>
#include <algorithm>

namespace
{
const auto service = QStringLiteral("org.bluez");
const auto adapterInterface = QStringLiteral("org.bluez.Adapter1");
const auto deviceInterface = QStringLiteral("org.bluez.Device1");
const auto batteryInterface = QStringLiteral("org.bluez.Battery1");
const auto agentManagerInterface = QStringLiteral("org.bluez.AgentManager1");
const auto agentPath = QStringLiteral("/org/platinum_reality/BluetoothAgent");
// Discovery in a crowded place finds hundreds of advertisers; keep the tree and lists bounded.
constexpr int maximumObjects = 1024;
constexpr int maximumNearby = 50;

// Only what the panel, strip and alerts show. Signal strength and advertising data change
// constantly while discovering and must not rebuild the device lists.
bool shown(const QString &interface, const QString &property)
{
    static const QSet<QString> adapter{"Powered", "Discovering", "Alias"};
    static const QSet<QString> device{"Alias", "Name", "Icon", "Paired", "Connected", "Trusted", "Adapter"};
    if (interface == adapterInterface)
        return adapter.contains(property);
    if (interface == deviceInterface)
        return device.contains(property);
    return interface == batteryInterface && property == "Percentage";
}

// BlueZ's freedesktop icon name: the panel's glyph and a short description.
std::pair<QString, QString> kindOf(const QString &icon)
{
    if (icon == "audio-headset" || icon == "audio-headphones")
        return {"headphones", "Headphones"};
    if (icon.startsWith("audio-") || icon == "multimedia-player")
        return {"speaker", "Audio"};
    if (icon == "input-mouse")
        return {"mouse", "Mouse"};
    if (icon == "input-keyboard")
        return {"keyboard", "Keyboard"};
    if (icon == "input-gaming")
        return {"gamepad", "Game Controller"};
    if (icon == "phone")
        return {"phone", "Phone"};
    if (icon == "computer")
        return {"computer", "Computer"};
    return {"device", icon == "input-tablet" ? "Tablet" : "Device"};
}

// Names come from the remote device: plain text, one line, bounded.
QString displayName(QString name)
{
    for (auto &c : name)
        if (c.category() == QChar::Other_Control || c == QChar::LineSeparator || c == QChar::ParagraphSeparator)
            c = QChar(' ');
    return name.simplified().left(80);
}

void sortByName(QVariantList &list)
{
    std::stable_sort(list.begin(), list.end(),
                     [](const QVariant &a, const QVariant &b)
                     {
                         return QString::compare(a.toMap().value("name").toString(), b.toMap().value("name").toString(),
                                                 Qt::CaseInsensitive) < 0;
                     });
}

bool failed(const QDBusMessage &reply)
{
    return reply.type() != QDBusMessage::ReplyMessage;
}
} // namespace

BluetoothAgent::BluetoothAgent(BluetoothService *service) : QObject(service), m_service(service)
{
}

bool BluetoothAgent::fromBluez()
{
    // The agent is reachable by anyone on the bus who can address it; only BlueZ may ask.
    if (!m_service->m_owner.isEmpty() && message().service() == m_service->m_owner)
        return true;
    sendErrorReply(QDBusError::AccessDenied, "Only BlueZ may use this pairing agent.");
    return false;
}

void BluetoothAgent::ask(const QString &kind, const QDBusObjectPath &device, const QString &code)
{
    setDelayedReply(true);
    m_service->agentRequest(kind, device.path(), code, -1, message());
}

void BluetoothAgent::Release()
{
    if (fromBluez())
        m_service->agentReleased();
}

QString BluetoothAgent::RequestPinCode(const QDBusObjectPath &device)
{
    if (fromBluez())
        ask("pin", device);
    return {};
}

void BluetoothAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pincode)
{
    if (fromBluez())
        m_service->agentRequest("display-pin", device.path(), pincode.left(16), -1, std::nullopt);
}

uint BluetoothAgent::RequestPasskey(const QDBusObjectPath &device)
{
    if (fromBluez())
        ask("passkey", device);
    return 0;
}

void BluetoothAgent::DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered)
{
    if (fromBluez())
        m_service->agentRequest("display-passkey", device.path(), QString("%1").arg(passkey % 1000000, 6, 10, QChar('0')),
                                entered, std::nullopt);
}

void BluetoothAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey)
{
    if (fromBluez())
        ask("confirm", device, QString("%1").arg(passkey % 1000000, 6, 10, QChar('0')));
}

void BluetoothAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    if (fromBluez())
        ask("authorize-pairing", device);
}

void BluetoothAgent::AuthorizeService(const QDBusObjectPath &device, const QString &)
{
    if (fromBluez())
        ask("authorize-service", device);
}

void BluetoothAgent::Cancel()
{
    if (fromBluez())
        m_service->agentCanceled();
}

BluetoothService::BluetoothService(QDBusConnection bus, bool defaultAgent, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_defaultAgent(defaultAgent)
{
    qDBusRegisterMetaType<BluetoothInterfaces>();
    qDBusRegisterMetaType<BluetoothObjects>();
    m_changed.setSingleShot(true);
    m_changed.setInterval(50);
    connect(&m_changed, &QTimer::timeout, this, &BluetoothService::changed);
    // Looking for devices uses the radio heavily; stop if the Add Device sheet is left open.
    m_discoveryLimit.setSingleShot(true);
    m_discoveryLimit.setInterval(120000);
    connect(&m_discoveryLimit, &QTimer::timeout, this, &BluetoothService::stopDiscovery);
    if (!m_bus.isConnected())
        return;
    m_agent = new BluetoothAgent(this);
    m_exported = m_bus.registerObject(agentPath, m_agent, QDBusConnection::ExportAllSlots);
    // QtDBus delivers these only from the current owner of org.bluez.
    m_bus.connect(service, QString(), "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
                  SLOT(interfacesAdded(QDBusObjectPath, BluetoothInterfaces)));
    m_bus.connect(service, QString(), "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this,
                  SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
    m_bus.connect(service, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    auto *watcher = new QDBusServiceWatcher(service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) { ownerChanged(owner); });
    auto query = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                                "GetNameOwner");
    query << service;
    auto *pending = new QDBusPendingCallWatcher(m_bus.asyncCall(query, 10000), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this,
            [this, pending]
            {
                pending->deleteLater();
                const auto reply = pending->reply();
                // BlueZ may not be running yet; the watcher reports it when it starts.
                if (m_owner.isEmpty() && !failed(reply) && !reply.arguments().isEmpty())
                    ownerChanged(reply.arguments().first().toString());
            });
}

BluetoothService::~BluetoothService()
{
    if (m_request && m_request->pending)
        m_bus.send(m_request->pending->createErrorReply("org.bluez.Error.Canceled", "The desktop is closing."));
    if (!m_bus.isConnected())
        return;
    if (m_discoveryRequested && !m_adapter.isEmpty())
        m_bus.send(QDBusMessage::createMethodCall(service, m_adapter, adapterInterface, "StopDiscovery"));
    if (m_agentRegistered)
    {
        auto release = QDBusMessage::createMethodCall(service, "/org/bluez", agentManagerInterface, "UnregisterAgent");
        release << QVariant::fromValue(QDBusObjectPath(agentPath));
        m_bus.send(release);
    }
    if (m_exported)
        m_bus.unregisterObject(agentPath);
}

void BluetoothService::call(const QString &path, const QString &interface, const QString &method,
                            const QVariantList &arguments, int timeout, std::function<void(const QDBusMessage &)> done)
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, timeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, method, generation = m_generation, done = std::move(done)]
            {
                watcher->deleteLater();
                const auto reply = watcher->reply();
                if (failed(reply))
                    qWarning("Bluetooth: %s: %s", qPrintable(method), qPrintable(reply.errorName()));
                // Replies from before a BlueZ restart refer to objects that are gone.
                if (done && generation == m_generation)
                    done(reply);
            });
}

void BluetoothService::writeProperty(const QString &path, const QString &interface, const QString &name,
                                     const QVariant &value, std::function<void(const QDBusMessage &)> done)
{
    call(path, "org.freedesktop.DBus.Properties", "Set", {interface, name, QVariant::fromValue(QDBusVariant(value))},
         10000, std::move(done));
}

void BluetoothService::ownerChanged(const QString &owner)
{
    ++m_generation;
    m_owner = owner;
    m_objects.clear();
    m_busy.clear();
    m_adapter.clear();
    m_pairing.clear();
    m_agentRegistered = m_discoveryRequested = false;
    m_discoveryLimit.stop();
    // BlueZ stopped or restarted: its questions went with it.
    if (m_request)
        finishRequest();
    scheduleChanged();
    if (owner.isEmpty())
        return;
    reload();
    registerAgent();
}

void BluetoothService::reload()
{
    call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", {}, 10000,
         [this](const QDBusMessage &reply)
         {
             if (failed(reply) || reply.arguments().isEmpty())
                 return;
             const auto objects = qdbus_cast<BluetoothObjects>(reply.arguments().first());
             m_objects.clear();
             for (auto it = objects.cbegin(); it != objects.cend() && m_objects.size() < maximumObjects; ++it)
                 m_objects.insert(it.key().path(), it.value());
             chooseAdapter();
             scheduleChanged();
         });
}

void BluetoothService::registerAgent()
{
    if (!m_exported)
        return;
    const auto path = QVariant::fromValue(QDBusObjectPath(agentPath));
    call("/org/bluez", agentManagerInterface, "RegisterAgent", {path, QString("KeyboardDisplay")}, 10000,
         [this, path](const QDBusMessage &reply)
         {
             if (failed(reply) && reply.errorName() != "org.bluez.Error.AlreadyExists")
                 return;
             // Registered, BlueZ asks this agent about pairings started here. The default agent
             // also answers devices that start pairing or connecting by themselves.
             m_agentRegistered = true;
             scheduleChanged();
             if (m_defaultAgent)
                 call("/org/bluez", agentManagerInterface, "RequestDefaultAgent", {path}, 10000);
         });
}

void BluetoothService::chooseAdapter()
{
    // Adapters sort by path, so hci0 is the first; devices on other adapters are not listed.
    QString adapter;
    for (auto it = m_objects.cbegin(); it != m_objects.cend() && adapter.isEmpty(); ++it)
        if (it->contains(adapterInterface))
            adapter = it.key();
    if (adapter == m_adapter)
        return;
    m_adapter = adapter;
    m_discoveryRequested = false;
    m_discoveryLimit.stop();
    scheduleChanged();
}

void BluetoothService::scheduleChanged()
{
    if (!m_changed.isActive())
        m_changed.start();
}

void BluetoothService::setMessage(const QString &text)
{
    if (m_message == text)
        return;
    m_message = text;
    emit messageChanged();
}

void BluetoothService::setBusy(const QString &path, const QString &state)
{
    if (state.isEmpty())
        m_busy.remove(path);
    else
        m_busy.insert(path, state);
    scheduleChanged();
}

bool BluetoothService::powered() const
{
    return m_objects.value(m_adapter).value(adapterInterface).value("Powered").toBool();
}

bool BluetoothService::discovering() const
{
    return m_objects.value(m_adapter).value(adapterInterface).value("Discovering").toBool();
}

QString BluetoothService::computerName() const
{
    return displayName(m_objects.value(m_adapter).value(adapterInterface).value("Alias").toString());
}

QString BluetoothService::nameOf(const QString &path) const
{
    const auto device = m_objects.value(path).value(deviceInterface);
    for (const auto *property : {"Alias", "Name", "Address"})
        if (const auto name = displayName(device.value(property).toString()); !name.isEmpty())
            return name;
    return "Bluetooth device";
}

bool BluetoothService::onAdapter(const QString &path) const
{
    if (m_adapter.isEmpty())
        return false;
    const auto adapter = m_objects.value(path).value(deviceInterface).value("Adapter");
    return adapter.isValid() ? qvariant_cast<QDBusObjectPath>(adapter).path() == m_adapter
                             : path.startsWith(m_adapter + "/");
}

QVariantList BluetoothService::devices() const
{
    QVariantList result;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
    {
        const auto device = it->value(deviceInterface);
        if (device.isEmpty() || !device.value("Paired").toBool() || !onAdapter(it.key()))
            continue;
        const auto [kind, type] = kindOf(device.value("Icon").toString());
        const auto battery = it->value(batteryInterface).value("Percentage");
        result.append(QVariantMap{{"path", it.key()},
                                  {"name", nameOf(it.key())},
                                  {"kind", kind},
                                  {"type", type},
                                  {"connected", device.value("Connected").toBool()},
                                  {"trusted", device.value("Trusted").toBool()},
                                  {"battery", battery.isValid() ? std::clamp(battery.toInt(), 0, 100) : -1},
                                  {"busy", m_busy.value(it.key())}});
    }
    sortByName(result);
    return result;
}

QVariantList BluetoothService::nearby() const
{
    QVariantList result;
    for (auto it = m_objects.cbegin(); it != m_objects.cend() && result.size() < maximumNearby; ++it)
    {
        const auto device = it->value(deviceInterface);
        // Unnamed advertisers (beacons, other people's phones' random addresses) are not offered.
        if (device.isEmpty() || device.value("Paired").toBool() || !onAdapter(it.key()) ||
            displayName(device.value("Name").toString()).isEmpty())
            continue;
        const auto [kind, type] = kindOf(device.value("Icon").toString());
        result.append(QVariantMap{{"path", it.key()},
                                  {"name", nameOf(it.key())},
                                  {"kind", kind},
                                  {"type", type},
                                  {"busy", m_busy.value(it.key())}});
    }
    sortByName(result);
    return result;
}

QVariantMap BluetoothService::request() const
{
    if (!m_request)
        return {};
    return {{"id", m_request->id},     {"kind", m_request->kind}, {"path", m_request->path},
            {"name", nameOf(m_request->path)}, {"code", m_request->code}, {"entered", m_request->entered}};
}

void BluetoothService::setPowered(bool on)
{
    if (m_adapter.isEmpty())
        return;
    setMessage({});
    writeProperty(m_adapter, adapterInterface, "Powered", on,
                  [this, on](const QDBusMessage &reply)
                  {
                      if (failed(reply))
                          setMessage(on ? "Bluetooth could not be turned on. Make sure airplane mode is off."
                                        : "Bluetooth could not be turned off.");
                  });
}

void BluetoothService::connectDevice(const QString &path)
{
    if (!m_objects.value(path).contains(deviceInterface) || m_busy.contains(path))
        return;
    setMessage({});
    setBusy(path, "connecting");
    call(path, deviceInterface, "Connect", {}, 60000,
         [this, path](const QDBusMessage &reply)
         {
             setBusy(path, {});
             if (!failed(reply) || reply.errorName() == "org.bluez.Error.AlreadyConnected")
                 return;
             setMessage(reply.errorName() == "org.bluez.Error.NotReady"
                            ? QString("Turn Bluetooth on to connect “%1”.").arg(nameOf(path))
                            : QString("“%1” could not connect. Make sure it is turned on and nearby.").arg(nameOf(path)));
         });
}

void BluetoothService::disconnectDevice(const QString &path)
{
    if (!m_objects.value(path).contains(deviceInterface) || m_busy.contains(path))
        return;
    setMessage({});
    setBusy(path, "disconnecting");
    call(path, deviceInterface, "Disconnect", {}, 30000,
         [this, path](const QDBusMessage &reply)
         {
             setBusy(path, {});
             if (failed(reply) && reply.errorName() != "org.bluez.Error.NotConnected")
                 setMessage(QString("“%1” could not be disconnected.").arg(nameOf(path)));
         });
}

void BluetoothService::forgetDevice(const QString &path)
{
    if (m_adapter.isEmpty() || !m_objects.value(path).contains(deviceInterface) || m_busy.contains(path))
        return;
    setMessage({});
    setBusy(path, "forgetting");
    const auto name = nameOf(path);
    call(m_adapter, adapterInterface, "RemoveDevice", {QVariant::fromValue(QDBusObjectPath(path))}, 30000,
         [this, path, name](const QDBusMessage &reply)
         {
             setBusy(path, {});
             if (failed(reply))
                 setMessage(QString("“%1” could not be forgotten.").arg(name));
         });
}

void BluetoothService::pairDevice(const QString &path)
{
    if (!m_pairing.isEmpty() || !m_objects.value(path).contains(deviceInterface) || m_busy.contains(path))
        return;
    // Discovery slows pairing and connecting on many adapters.
    stopDiscovery();
    setMessage({});
    m_pairing = path;
    setBusy(path, "pairing");
    call(path, deviceInterface, "Pair", {}, 120000,
         [this, path](const QDBusMessage &reply)
         {
             m_pairing.clear();
             setBusy(path, {});
             // A code shown for typing on the device is finished either way.
             if (m_request && !m_request->pending && m_request->path == path)
                 finishRequest();
             const auto error = reply.errorName();
             if (failed(reply) && error != "org.bluez.Error.AlreadyExists")
             {
                 const auto name = nameOf(path);
                 setMessage(error == "org.bluez.Error.AuthenticationCanceled" ||
                                    error == "org.bluez.Error.AuthenticationRejected"
                                ? QString("Pairing with “%1” was canceled.").arg(name)
                            : error == "org.bluez.Error.AuthenticationFailed"
                                ? QString("Pairing with “%1” failed. Check the code and try again.").arg(name)
                            : error == "org.bluez.Error.AuthenticationTimeout"
                                ? QString("“%1” did not answer in time. Put it in pairing mode and try again.").arg(name)
                                : QString("Pairing with “%1” failed. Make sure it is in pairing mode and nearby.").arg(name));
                 return;
             }
             // As KDE does after pairing: trust the device so it can reconnect by itself, then connect.
             writeProperty(path, deviceInterface, "Trusted", true);
             connectDevice(path);
             emit paired(path);
         });
}

void BluetoothService::startDiscovery()
{
    if (m_adapter.isEmpty() || m_discoveryRequested)
        return;
    m_discoveryRequested = true;
    m_discoveryLimit.start();
    setMessage({});
    call(m_adapter, adapterInterface, "StartDiscovery", {}, 10000,
         [this](const QDBusMessage &reply)
         {
             if (!failed(reply) || reply.errorName() == "org.bluez.Error.InProgress")
                 return;
             m_discoveryRequested = false;
             m_discoveryLimit.stop();
             setMessage(reply.errorName() == "org.bluez.Error.NotReady" ? "Turn Bluetooth on to look for devices."
                                                                         : "Bluetooth could not look for devices.");
         });
}

void BluetoothService::stopDiscovery()
{
    m_discoveryLimit.stop();
    if (!m_discoveryRequested || m_adapter.isEmpty())
        return;
    m_discoveryRequested = false;
    call(m_adapter, adapterInterface, "StopDiscovery", {}, 10000);
}

void BluetoothService::answer(bool accept, const QString &value, bool always)
{
    if (!m_request)
        return;
    const auto request = *m_request;
    finishRequest();
    if (!request.pending)
    {
        // A code shown for typing on the device: Cancel stops the pairing.
        if (!accept)
            call(request.path, deviceInterface, "CancelPairing", {}, 10000);
        return;
    }
    const auto &message = *request.pending;
    const auto reject = [&] { m_bus.send(message.createErrorReply("org.bluez.Error.Rejected", "Declined.")); };
    if (!accept)
        return reject();
    const auto text = value.trimmed();
    if (request.kind == "pin")
    {
        const bool valid = !text.isEmpty() && text.size() <= 16 &&
                           std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() > 32 && c.unicode() < 127; });
        if (!valid)
        {
            setMessage("A PIN is 1 to 16 letters or digits.");
            return reject();
        }
        m_bus.send(message.createReply(text));
    }
    else if (request.kind == "passkey")
    {
        bool number = false;
        const auto passkey = text.toUInt(&number);
        if (!number || passkey > 999999)
        {
            setMessage("A passkey is a number of up to six digits.");
            return reject();
        }
        m_bus.send(message.createReply(QVariant::fromValue(passkey)));
    }
    else
    {
        if (request.kind == "authorize-service" && always)
            writeProperty(request.path, deviceInterface, "Trusted", true);
        m_bus.send(message.createReply());
    }
}

void BluetoothService::agentRequest(const QString &kind, const QString &path, const QString &code, int entered,
                                    std::optional<QDBusMessage> pending)
{
    if (m_request && m_request->pending)
    {
        // BlueZ asks one question at a time; decline anything overlapping the one on screen.
        if (pending)
            m_bus.send(pending->createErrorReply("org.bluez.Error.Rejected", "Another request is being answered."));
        return;
    }
    if (m_request && !pending && m_request->kind == kind && m_request->path == path)
    {
        // DisplayPasskey repeats as digits are typed on the device.
        m_request->code = code;
        m_request->entered = entered;
        emit requestChanged();
        return;
    }
    m_request = Request{++m_nextRequest, kind, path, code, entered, std::move(pending)};
    emit requestChanged();
}

void BluetoothService::agentCanceled()
{
    // BlueZ withdrew its question (timed out, or the device gave up); no answer is expected.
    if (m_request)
        finishRequest();
}

void BluetoothService::agentReleased()
{
    m_agentRegistered = false;
    if (m_request)
        finishRequest();
    scheduleChanged();
}

void BluetoothService::finishRequest()
{
    m_request.reset();
    emit requestChanged();
}

void BluetoothService::interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces)
{
    if (!m_objects.contains(path.path()) && m_objects.size() >= maximumObjects)
        return;
    auto &object = m_objects[path.path()];
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
        object.insert(it.key(), it.value());
    if (interfaces.contains(adapterInterface))
        chooseAdapter();
    scheduleChanged();
}

void BluetoothService::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    const auto it = m_objects.find(path.path());
    if (it == m_objects.end())
        return;
    for (const auto &interface : interfaces)
        it->remove(interface);
    if (it->isEmpty())
        m_objects.erase(it);
    if (interfaces.contains(deviceInterface))
        m_busy.remove(path.path());
    if (interfaces.contains(adapterInterface))
        chooseAdapter();
    scheduleChanged();
}

void BluetoothService::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                         const QStringList &invalidated, const QDBusMessage &message)
{
    const auto it = m_objects.find(message.path());
    if (it == m_objects.end() || !it->contains(interface))
        return;
    auto &properties = (*it)[interface];
    bool visible = false;
    for (auto property = changed.cbegin(); property != changed.cend(); ++property)
    {
        properties.insert(property.key(), property.value());
        visible = visible || shown(interface, property.key());
    }
    for (const auto &name : invalidated)
    {
        properties.remove(name);
        visible = visible || shown(interface, name);
    }
    // A code shown for typing on the device is finished once the device is paired.
    if (interface == deviceInterface && changed.value("Paired").toBool() && m_request && !m_request->pending &&
        m_request->path == message.path())
        finishRequest();
    if (visible)
        scheduleChanged();
}
