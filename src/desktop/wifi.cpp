#include "wifi.hpp"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QRegularExpression>
#include <QSet>

namespace
{
const auto service = QStringLiteral("net.connman.iwd");
const auto stationInterface = QStringLiteral("net.connman.iwd.Station");
const auto networkInterface = QStringLiteral("net.connman.iwd.Network");
const auto knownInterface = QStringLiteral("net.connman.iwd.KnownNetwork");
const auto agentManagerInterface = QStringLiteral("net.connman.iwd.AgentManager");
const auto agentManagerPath = QStringLiteral("/net/connman/iwd");
const auto agentPath = QStringLiteral("/org/platinum_reality/WifiAgent");
const auto canceled = QStringLiteral("net.connman.iwd.Agent.Error.Canceled");
// A busy place has hundreds of access points (each one an object); keep the tree and list bounded.
constexpr int maximumObjects = 2048;
constexpr int maximumNetworks = 30;

// Only what the strip menu shows. Access point objects come and go constantly while scanning.
bool shown(const QString &interface, const QString &property)
{
    static const QSet<QString> station{"State", "Scanning", "ConnectedNetwork"};
    static const QSet<QString> network{"Name", "Connected", "KnownNetwork", "Type"};
    if (interface == stationInterface)
        return station.contains(property);
    return interface == networkInterface && network.contains(property);
}

// Network names come from the radio: plain text, one line, bounded.
QString displayName(QString name)
{
    for (auto &c : name)
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ||
            c == QChar::LineSeparator || c == QChar::ParagraphSeparator)
            c = QChar(' ');
    return name.simplified().left(64);
}

// Signal strength in 100 * dBm, as impala draws it.
int barsOf(int strength)
{
    return strength >= -6000 ? 4 : strength >= -6700 ? 3 : strength >= -7500 ? 2 : 1;
}

bool failed(const QDBusMessage &reply)
{
    return reply.type() != QDBusMessage::ReplyMessage;
}

QString objectPath(const QVariant &value)
{
    const auto path = qvariant_cast<QDBusObjectPath>(value).path();
    return path == "/" ? QString() : path;
}
} // namespace

WifiAgent::WifiAgent(WifiService *service) : QObject(service), m_service(service)
{
}

bool WifiAgent::fromIwd()
{
    // The agent is reachable by anyone on the bus who can address it; only iwd may ask.
    if (!m_service->m_owner.isEmpty() && message().service() == m_service->m_owner)
        return true;
    sendErrorReply(QDBusError::AccessDenied, "Only iwd may use this agent.");
    return false;
}

void WifiAgent::ask(const QString &kind, const QDBusObjectPath &network, const QString &user)
{
    setDelayedReply(true);
    m_service->agentRequest(kind, network.path(), user, message());
}

void WifiAgent::Release()
{
    if (fromIwd())
        m_service->agentReleased();
}

QString WifiAgent::RequestPassphrase(const QDBusObjectPath &network)
{
    if (fromIwd())
        ask("passphrase", network);
    return {};
}

QString WifiAgent::RequestPrivateKeyPassphrase(const QDBusObjectPath &network)
{
    if (fromIwd())
        ask("private-key", network);
    return {};
}

QString WifiAgent::RequestUserNameAndPassword(const QDBusObjectPath &network, QString &)
{
    if (fromIwd())
        ask("user-password", network);
    return {};
}

QString WifiAgent::RequestUserPassword(const QDBusObjectPath &network, const QString &user)
{
    if (fromIwd())
        ask("password", network, displayName(user));
    return {};
}

void WifiAgent::Cancel(const QString &)
{
    if (fromIwd())
        m_service->agentCanceled();
}

WifiService::WifiService(QDBusConnection bus, QObject *parent) : QObject(parent), m_bus(std::move(bus))
{
    qDBusRegisterMetaType<BluetoothInterfaces>();
    qDBusRegisterMetaType<BluetoothObjects>();
    m_changed.setSingleShot(true);
    m_changed.setInterval(50);
    connect(&m_changed, &QTimer::timeout, this, &WifiService::changed);
    // Networks appear and disappear in bursts during a scan; ask for the order once they settle.
    m_refresh.setSingleShot(true);
    m_refresh.setInterval(300);
    connect(&m_refresh, &QTimer::timeout, this, &WifiService::refreshNetworks);
    if (!m_bus.isConnected())
        return;
    m_agent = new WifiAgent(this);
    m_exported = m_bus.registerObject(agentPath, m_agent, QDBusConnection::ExportAllSlots);
    // QtDBus delivers these only from the current owner of net.connman.iwd.
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
                // iwd may not be running yet; the watcher reports it when it starts.
                if (m_owner.isEmpty() && !failed(reply) && !reply.arguments().isEmpty())
                    ownerChanged(reply.arguments().first().toString());
            });
}

WifiService::~WifiService()
{
    if (m_request)
        m_bus.send(m_request->pending.createErrorReply(canceled, "The desktop is closing."));
    if (!m_bus.isConnected())
        return;
    if (m_agentRegistered)
    {
        auto release = QDBusMessage::createMethodCall(service, agentManagerPath, agentManagerInterface, "UnregisterAgent");
        release << QVariant::fromValue(QDBusObjectPath(agentPath));
        m_bus.send(release);
    }
    if (m_exported)
        m_bus.unregisterObject(agentPath);
}

void WifiService::call(const QString &path, const QString &interface, const QString &method,
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
                    qWarning("Wi-Fi: %s: %s", qPrintable(method), qPrintable(reply.errorName()));
                // Replies from before an iwd restart refer to objects that are gone.
                if (done && generation == m_generation)
                    done(reply);
            });
}

void WifiService::ownerChanged(const QString &owner)
{
    ++m_generation;
    m_owner = owner;
    m_objects.clear();
    m_ordered.clear();
    m_station.clear();
    m_joining.clear();
    m_agentRegistered = false;
    // iwd stopped or restarted: its questions went with it.
    if (m_request)
        finishRequest();
    scheduleChanged();
    if (owner.isEmpty())
        return;
    reload();
    registerAgent();
}

void WifiService::reload()
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
             chooseStation();
             refreshNetworks();
         });
}

void WifiService::registerAgent()
{
    if (!m_exported)
        return;
    call(agentManagerPath, agentManagerInterface, "RegisterAgent", {QVariant::fromValue(QDBusObjectPath(agentPath))},
         10000,
         [this](const QDBusMessage &reply)
         {
             if (failed(reply) && reply.errorName() != "net.connman.iwd.AlreadyExists")
                 return;
             m_agentRegistered = true;
             scheduleChanged();
         });
}

void WifiService::chooseStation()
{
    // Stations sort by path, so the first device is chosen, as impala does.
    QString station;
    for (auto it = m_objects.cbegin(); it != m_objects.cend() && station.isEmpty(); ++it)
        if (it->contains(stationInterface))
            station = it.key();
    if (station == m_station)
        return;
    m_station = station;
    m_ordered.clear();
    scheduleChanged();
}

void WifiService::refreshNetworks()
{
    if (m_station.isEmpty())
    {
        if (!m_ordered.isEmpty())
        {
            m_ordered.clear();
            scheduleChanged();
        }
        return;
    }
    call(m_station, stationInterface, "GetOrderedNetworks", {}, 10000,
         [this, station = m_station](const QDBusMessage &reply)
         {
             if (failed(reply) || reply.arguments().isEmpty() || station != m_station)
                 return;
             // a(on): network paths with signal strengths, strongest first.
             QList<std::pair<QString, int>> ordered;
             const auto argument = reply.arguments().first().value<QDBusArgument>();
             argument.beginArray();
             while (!argument.atEnd())
             {
                 QDBusObjectPath path;
                 qint16 strength = 0;
                 argument.beginStructure();
                 argument >> path >> strength;
                 argument.endStructure();
                 if (ordered.size() < maximumNetworks)
                     ordered.append({path.path(), strength});
             }
             argument.endArray();
             m_ordered = ordered;
             scheduleChanged();
         });
}

void WifiService::scheduleChanged()
{
    if (!m_changed.isActive())
        m_changed.start();
}

void WifiService::setMessage(const QString &text)
{
    if (m_message == text)
        return;
    m_message = text;
    emit messageChanged();
}

bool WifiService::scanning() const
{
    return m_objects.value(m_station).value(stationInterface).value("Scanning").toBool();
}

QString WifiService::nameOf(const QString &path) const
{
    const auto name = displayName(m_objects.value(path).value(networkInterface).value("Name").toString());
    return name.isEmpty() ? QString("Wi-Fi network") : name;
}

QVariantList WifiService::networks() const
{
    QVariantList result;
    for (const auto &[path, strength] : m_ordered)
    {
        const auto network = m_objects.value(path).value(networkInterface);
        // Hidden networks have no name to choose by.
        if (network.isEmpty() || displayName(network.value("Name").toString()).isEmpty())
            continue;
        const auto type = network.value("Type").toString();
        result.append(QVariantMap{{"path", path},
                                  {"name", nameOf(path)},
                                  {"type", type},
                                  {"secure", type != "open"},
                                  {"bars", barsOf(strength)},
                                  {"connected", network.value("Connected").toBool()},
                                  {"known", !objectPath(network.value("KnownNetwork")).isEmpty()},
                                  {"busy", path == m_joining ? "joining" : ""}});
    }
    return result;
}

QVariantMap WifiService::request() const
{
    if (!m_request)
        return {};
    return {{"id", m_request->id},
            {"kind", m_request->kind},
            {"path", m_request->path},
            {"name", nameOf(m_request->path)},
            {"type", m_objects.value(m_request->path).value(networkInterface).value("Type").toString()},
            {"user", m_request->user}};
}

void WifiService::connectNetwork(const QString &path)
{
    if (!m_objects.value(path).contains(networkInterface) || !m_joining.isEmpty())
        return;
    setMessage({});
    m_joining = path;
    scheduleChanged();
    const auto name = nameOf(path);
    const bool secure = m_objects.value(path).value(networkInterface).value("Type").toString() != "open";
    // iwd answers once the network is joined, after asking the agent for a password if it needs one.
    call(path, networkInterface, "Connect", {}, 180000,
         [this, path, name, secure](const QDBusMessage &reply)
         {
             if (m_joining == path)
                 m_joining.clear();
             scheduleChanged();
             const auto error = reply.errorName();
             // Canceled in the password alert, or superseded by another join: nothing to say.
             if (!failed(reply) || error == "net.connman.iwd.Aborted" || error == canceled ||
                 error == "net.connman.iwd.InProgress" || error == "net.connman.iwd.AlreadyConnected")
                 return;
             setMessage(error == "net.connman.iwd.NotSupported"
                            ? QString("“%1” uses a kind of security this computer cannot join.").arg(name)
                        : error == "net.connman.iwd.NotFound" || error == "net.connman.iwd.NotAvailable"
                            ? QString("“%1” is no longer in range.").arg(name)
                        : secure && error == "net.connman.iwd.Failed"
                            ? QString("Could not join “%1”. Check the password and try again.").arg(name)
                            : QString("Could not join “%1”.").arg(name));
         });
}

void WifiService::disconnectNetwork()
{
    if (m_station.isEmpty())
        return;
    setMessage({});
    call(m_station, stationInterface, "Disconnect", {}, 30000,
         [this](const QDBusMessage &reply)
         {
             if (failed(reply) && reply.errorName() != "net.connman.iwd.NotConnected")
                 setMessage("Wi-Fi could not disconnect.");
         });
}

void WifiService::forgetNetwork(const QString &path)
{
    const auto known = objectPath(m_objects.value(path).value(networkInterface).value("KnownNetwork"));
    if (known.isEmpty())
        return;
    setMessage({});
    const auto name = nameOf(path);
    call(known, knownInterface, "Forget", {}, 30000,
         [this, name](const QDBusMessage &reply)
         {
             if (failed(reply))
                 setMessage(QString("“%1” could not be forgotten.").arg(name));
         });
}

void WifiService::scan()
{
    if (m_station.isEmpty() || scanning())
        return;
    // A scan already running (iwd scans by itself too) or refused while joining is fine.
    call(m_station, stationInterface, "Scan", {}, 30000);
}

void WifiService::answer(bool accept, const QString &password, const QString &user)
{
    if (!m_request)
        return;
    const auto request = *m_request;
    finishRequest();
    const auto reject = [&] { m_bus.send(request.pending.createErrorReply(canceled, "Canceled.")); };
    if (!accept || password.isEmpty() || password.size() > 1024 || user.size() > 1024)
        return reject();
    if (request.kind == "passphrase" &&
        m_objects.value(request.path).value(networkInterface).value("Type").toString() == "psk")
    {
        // WPA: 8 to 63 characters, or the 64 hexadecimal digits of the key itself.
        static const QRegularExpression key("^[0-9A-Fa-f]{64}$");
        if ((password.size() < 8 || password.size() > 63) && !key.match(password).hasMatch())
        {
            setMessage("A Wi-Fi password has 8 to 63 characters.");
            return reject();
        }
    }
    if (request.kind == "user-password")
    {
        if (user.isEmpty())
            return reject();
        auto reply = request.pending.createReply();
        reply << user << password;
        m_bus.send(reply);
        return;
    }
    m_bus.send(request.pending.createReply(password));
}

void WifiService::agentRequest(const QString &kind, const QString &path, const QString &user, const QDBusMessage &pending)
{
    if (m_request)
    {
        // iwd asks one question at a time; decline anything overlapping the one on screen.
        m_bus.send(pending.createErrorReply(canceled, "Another request is being answered."));
        return;
    }
    m_request = Request{++m_nextRequest, kind, path, user, pending};
    emit requestChanged();
}

void WifiService::agentCanceled()
{
    // iwd withdrew its question (timed out, or the join was abandoned); no answer is expected.
    if (m_request)
        finishRequest();
}

void WifiService::agentReleased()
{
    m_agentRegistered = false;
    if (m_request)
        finishRequest();
    scheduleChanged();
}

void WifiService::finishRequest()
{
    m_request.reset();
    emit requestChanged();
}

void WifiService::interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces)
{
    if (!m_objects.contains(path.path()) && m_objects.size() >= maximumObjects)
        return;
    auto &object = m_objects[path.path()];
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
        object.insert(it.key(), it.value());
    if (interfaces.contains(stationInterface))
        chooseStation();
    if (interfaces.contains(stationInterface) || interfaces.contains(networkInterface))
        m_refresh.start();
}

void WifiService::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    const auto it = m_objects.find(path.path());
    if (it == m_objects.end())
        return;
    for (const auto &interface : interfaces)
        it->remove(interface);
    if (it->isEmpty())
        m_objects.erase(it);
    if (interfaces.contains(stationInterface))
        chooseStation();
    if (interfaces.contains(stationInterface) || interfaces.contains(networkInterface))
        m_refresh.start();
}

void WifiService::propertiesChanged(const QString &interface, const QVariantMap &changed,
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
    // A finished scan, or a join or leave, reorders the networks.
    if (interface == stationInterface && message.path() == m_station &&
        ((changed.contains("Scanning") && !changed.value("Scanning").toBool()) || changed.contains("State")))
        m_refresh.start();
    if (visible)
        scheduleChanged();
}
