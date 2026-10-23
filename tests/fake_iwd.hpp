#pragma once
#include "desktop/wifi.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVirtualObject>
#include <optional>

// One entry of Station.GetOrderedNetworks: a network and its signal strength (100 * dBm).
struct FakeIwdOrdered
{
    QDBusObjectPath path;
    qint16 strength = 0;
};
Q_DECLARE_METATYPE(FakeIwdOrdered)
inline QDBusArgument &operator<<(QDBusArgument &argument, const FakeIwdOrdered &entry)
{
    argument.beginStructure();
    argument << entry.path << entry.strength;
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, FakeIwdOrdered &entry)
{
    argument.beginStructure();
    argument >> entry.path >> entry.strength;
    argument.endStructure();
    return argument;
}

// iwd's D-Bus surface, enough for one station, its networks, known networks and a password
// agent: the object manager, properties with change signals, Station, Network, KnownNetwork and
// AgentManager. It answers from the test's event loop, records every call, and asks the
// registered agent for a password as iwd does when joining a secured network it does not know.
class FakeIwd final : public QDBusVirtualObject
{
  public:
    static constexpr auto station = "/net/connman/iwd/0/3";
    static constexpr auto network = "net.connman.iwd.Network";
    QStringList calls;
    QString agentOwner, agentPath;
    // The password each secured network accepts.
    QMap<QString, QString> passwords;

    explicit FakeIwd(QDBusConnection bus) : m_bus(std::move(bus))
    {
        qDBusRegisterMetaType<BluetoothInterfaces>();
        qDBusRegisterMetaType<BluetoothObjects>();
        qDBusRegisterMetaType<FakeIwdOrdered>();
        qDBusRegisterMetaType<QList<FakeIwdOrdered>>();
        m_objects["/net/connman/iwd"]["net.connman.iwd.AgentManager"] = {};
        m_objects[station]["net.connman.iwd.Station"] = {
            {"State", "disconnected"}, {"Scanning", false}};
    }
    // A network in range; known networks have a KnownNetwork object, as iwd keeps them.
    QString addNetwork(const QString &name, const QString &type, int strength, bool known = false,
                       bool connected = false, bool announce = false)
    {
        const auto id = QString::fromLatin1(name.toUtf8().toHex()) + "_" + type;
        const auto path = QString(station) + "/" + id;
        m_objects[path][network] = {{"Name", name},
                                    {"Type", type},
                                    {"Connected", connected},
                                    {"Device", QVariant::fromValue(QDBusObjectPath(station))}};
        m_strength[path] = strength;
        if (known)
            remember(path, false);
        if (connected)
            setConnected(path);
        if (announce)
            added(path);
        return path;
    }
    QString knownPath(const QString &path) const
    {
        return qvariant_cast<QDBusObjectPath>(value(path, network, "KnownNetwork")).path();
    }
    QVariant value(const QString &path, const QString &interface, const QString &name) const
    {
        return m_objects.value(path).value(interface).value(name);
    }
    void change(const QString &path, const QString &interface, const QString &name, const QVariant &value)
    {
        m_objects[path][interface][name] = value;
        auto signal = QDBusMessage::createSignal(path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
        signal << interface << QVariantMap{{name, value}} << QStringList();
        m_bus.send(signal);
    }
    void finishScan() { change(station, "net.connman.iwd.Station", "Scanning", false); }
    // A question to the agent, as iwd asks it (or, with another connection, as an impostor would).
    QDBusPendingCall ask(const QString &method, const QVariantList &arguments, std::optional<QDBusConnection> from = {})
    {
        auto call = QDBusMessage::createMethodCall(agentOwner, agentPath, "net.connman.iwd.Agent", method);
        call.setArguments(arguments);
        return (from ? *from : m_bus).asyncCall(call, 120000);
    }

    QString introspect(const QString &) const override { return {}; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override
    {
        const auto path = message.path(), interface = message.interface(), member = message.member();
        const auto arguments = message.arguments();
        const auto reply = [&](const QVariantList &values = {})
        {
            auto answer = message.createReply();
            answer.setArguments(values);
            m_bus.send(answer);
            return true;
        };
        const auto error = [&](const QString &name)
        {
            m_bus.send(message.createErrorReply(name, "Fake iwd"));
            return true;
        };
        if (interface == "org.freedesktop.DBus.ObjectManager" && member == "GetManagedObjects" && path == "/")
        {
            calls << member;
            BluetoothObjects objects;
            for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it)
                objects.insert(QDBusObjectPath(it.key()), it.value());
            return reply({QVariant::fromValue(objects)});
        }
        if (interface == "net.connman.iwd.AgentManager" && path == "/net/connman/iwd")
        {
            calls << member;
            if (member == "RegisterAgent")
            {
                agentOwner = message.service();
                agentPath = qvariant_cast<QDBusObjectPath>(arguments.value(0)).path();
            }
            return reply();
        }
        if (interface == "net.connman.iwd.Station" && path == station)
        {
            calls << member;
            if (member == "GetOrderedNetworks")
            {
                QList<FakeIwdOrdered> ordered;
                for (auto it = m_strength.cbegin(); it != m_strength.cend(); ++it)
                    if (m_objects.contains(it.key()))
                        ordered.append({QDBusObjectPath(it.key()), qint16(it.value())});
                std::stable_sort(ordered.begin(), ordered.end(),
                                 [](const auto &a, const auto &b) { return a.strength > b.strength; });
                return reply({QVariant::fromValue(ordered)});
            }
            if (member == "Scan")
            {
                change(station, "net.connman.iwd.Station", "Scanning", true);
                return reply();
            }
            if (member == "Disconnect")
            {
                const auto connected = qvariant_cast<QDBusObjectPath>(value(station, "net.connman.iwd.Station", "ConnectedNetwork")).path();
                if (connected.isEmpty() || connected == "/")
                    return error("net.connman.iwd.NotConnected");
                change(connected, network, "Connected", false);
                change(station, "net.connman.iwd.Station", "ConnectedNetwork", QVariant::fromValue(QDBusObjectPath("/")));
                change(station, "net.connman.iwd.Station", "State", "disconnected");
                return reply();
            }
        }
        if (interface == network && m_objects.value(path).contains(network) && member == "Connect")
        {
            calls << "Connect " + path;
            const auto type = value(path, network, "Type").toString();
            if (type == "open" || !knownPath(path).isEmpty())
            {
                setConnected(path);
                return reply();
            }
            // A secured network iwd does not know: the agent is asked for its password.
            if (agentOwner.isEmpty())
                return error("net.connman.iwd.NoAgent");
            auto *watcher = new QDBusPendingCallWatcher(ask("RequestPassphrase", {QVariant::fromValue(QDBusObjectPath(path))}), this);
            QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this,
                             [this, watcher, message, path]
                             {
                                 watcher->deleteLater();
                                 const auto answer = watcher->reply();
                                 if (answer.type() != QDBusMessage::ReplyMessage)
                                 {
                                     calls << "Canceled " + answer.errorName();
                                     m_bus.send(message.createErrorReply("net.connman.iwd.Aborted", "Fake iwd"));
                                     return;
                                 }
                                 const auto given = answer.arguments().value(0).toString();
                                 calls << "Passphrase " + given;
                                 if (given != passwords.value(path))
                                 {
                                     m_bus.send(message.createErrorReply("net.connman.iwd.Failed", "Fake iwd"));
                                     return;
                                 }
                                 remember(path, true);
                                 setConnected(path);
                                 m_bus.send(message.createReply());
                             });
            return true;
        }
        if (interface == "net.connman.iwd.KnownNetwork" && member == "Forget" && m_objects.contains(path))
        {
            calls << "Forget " + path;
            m_objects.remove(path);
            auto removed = QDBusMessage::createSignal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
            removed << QVariant::fromValue(QDBusObjectPath(path)) << QStringList{"net.connman.iwd.KnownNetwork"};
            m_bus.send(removed);
            for (auto it = m_objects.begin(); it != m_objects.end(); ++it)
                if (qvariant_cast<QDBusObjectPath>(it->value(network).value("KnownNetwork")).path() == path)
                {
                    (*it)[network].remove("KnownNetwork");
                    auto signal = QDBusMessage::createSignal(it.key(), "org.freedesktop.DBus.Properties", "PropertiesChanged");
                    signal << QString(network) << QVariantMap() << QStringList{"KnownNetwork"};
                    m_bus.send(signal);
                }
            return reply();
        }
        return false;
    }

  private:
    void added(const QString &path)
    {
        auto signal = QDBusMessage::createSignal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");
        signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant::fromValue(m_objects.value(path));
        m_bus.send(signal);
    }
    void remember(const QString &path, bool announce)
    {
        const auto known = "/net/connman/iwd/" + path.section('/', -1);
        m_objects[known]["net.connman.iwd.KnownNetwork"] = {{"Name", value(path, network, "Name")},
                                                            {"Type", value(path, network, "Type")}};
        if (announce)
        {
            added(known);
            change(path, network, "KnownNetwork", QVariant::fromValue(QDBusObjectPath(known)));
        }
        else
            m_objects[path][network]["KnownNetwork"] = QVariant::fromValue(QDBusObjectPath(known));
    }
    void setConnected(const QString &path)
    {
        const auto previous = qvariant_cast<QDBusObjectPath>(value(station, "net.connman.iwd.Station", "ConnectedNetwork")).path();
        if (!previous.isEmpty() && previous != "/" && previous != path)
            change(previous, network, "Connected", false);
        change(path, network, "Connected", true);
        change(station, "net.connman.iwd.Station", "ConnectedNetwork", QVariant::fromValue(QDBusObjectPath(path)));
        change(station, "net.connman.iwd.Station", "State", "connected");
    }
    QDBusConnection m_bus;
    QMap<QString, BluetoothInterfaces> m_objects;
    QMap<QString, int> m_strength;
};
