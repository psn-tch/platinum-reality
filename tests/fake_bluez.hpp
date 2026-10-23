#pragma once
#include "desktop/bluetooth.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QSet>
#include <optional>

// BlueZ's D-Bus surface, enough for one adapter, its devices and a pairing agent: the object
// manager, properties with change signals, Adapter1, Device1, Battery1 and AgentManager1. It
// answers from the test's event loop (the service never waits on the bus), records every call,
// and asks the registered agent the questions BlueZ would.
class FakeBluez final : public QDBusVirtualObject
{
  public:
    static constexpr auto adapter = "/org/bluez/hci0";
    static constexpr auto device = "org.bluez.Device1";
    QStringList calls;
    QString agentOwner, agentPath;
    bool defaultAgent = false;
    QSet<QString> failConnect;
    std::optional<QDBusMessage> pendingPair;

    explicit FakeBluez(QDBusConnection bus) : m_bus(std::move(bus))
    {
        qDBusRegisterMetaType<BluetoothInterfaces>();
        qDBusRegisterMetaType<BluetoothObjects>();
        m_objects["/org/bluez"]["org.bluez.AgentManager1"] = {};
        m_objects[adapter]["org.bluez.Adapter1"] = {
            {"Address", "00:11:22:33:44:55"}, {"Alias", "test-computer"}, {"Powered", true}, {"Discovering", false}};
    }
    QString addDevice(const QString &address, const QString &name, const QString &icon, bool paired, bool trusted,
                      bool connected, int battery = -1, bool announce = false)
    {
        const auto path = QString(adapter) + "/dev_" + QString(address).replace(':', '_');
        BluetoothInterfaces interfaces;
        interfaces[device] = {{"Address", address},
                              {"Alias", name.isEmpty() ? QString(address).replace(':', '-') : name},
                              {"Icon", icon},
                              {"Paired", paired},
                              {"Trusted", trusted},
                              {"Connected", connected},
                              {"Adapter", QVariant::fromValue(QDBusObjectPath(adapter))}};
        if (!name.isEmpty())
            interfaces[device]["Name"] = name;
        if (battery >= 0)
            interfaces["org.bluez.Battery1"] = {{"Percentage", QVariant::fromValue(uchar(battery))}};
        m_objects[path] = interfaces;
        if (announce)
        {
            auto added = QDBusMessage::createSignal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesAdded");
            added << QVariant::fromValue(QDBusObjectPath(path)) << QVariant::fromValue(interfaces);
            m_bus.send(added);
        }
        return path;
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
    // A question to the agent, as BlueZ asks it (or, with another connection, as an impostor would).
    QDBusPendingCall ask(const QString &method, const QVariantList &arguments, std::optional<QDBusConnection> from = {})
    {
        auto call = QDBusMessage::createMethodCall(agentOwner, agentPath, "org.bluez.Agent1", method);
        call.setArguments(arguments);
        return (from ? *from : m_bus).asyncCall(call, 120000);
    }
    void finishPair(bool paired, const QString &error = "org.bluez.Error.AuthenticationFailed")
    {
        if (!pendingPair)
            return;
        const auto message = *pendingPair;
        pendingPair.reset();
        if (!paired)
        {
            m_bus.send(message.createErrorReply(error, "Fake BlueZ"));
            return;
        }
        change(message.path(), device, "Paired", true);
        m_bus.send(message.createReply());
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
            m_bus.send(message.createErrorReply(name, "Fake BlueZ"));
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
        if (interface == "org.freedesktop.DBus.Properties" && member == "Set")
        {
            const auto target = arguments.value(0).toString(), name = arguments.value(1).toString();
            const auto value = qvariant_cast<QDBusVariant>(arguments.value(2)).variant();
            calls << QString("Set %1 %2 %3").arg(path, name, value.toString());
            if (!m_objects.value(path).contains(target))
                return error("org.freedesktop.DBus.Error.UnknownObject");
            change(path, target, name, value);
            return reply();
        }
        if (interface == "org.bluez.AgentManager1" && path == "/org/bluez")
        {
            calls << (member + " " + arguments.value(1).toString()).trimmed();
            if (member == "RegisterAgent")
            {
                agentOwner = message.service();
                agentPath = qvariant_cast<QDBusObjectPath>(arguments.value(0)).path();
            }
            defaultAgent = member == "RequestDefaultAgent" || (defaultAgent && member != "UnregisterAgent");
            return reply();
        }
        if (interface == "org.bluez.Adapter1" && path == adapter)
        {
            if (member == "StartDiscovery" || member == "StopDiscovery")
            {
                calls << member;
                change(adapter, "org.bluez.Adapter1", "Discovering", member == "StartDiscovery");
                return reply();
            }
            if (member == "RemoveDevice")
            {
                const auto removed = qvariant_cast<QDBusObjectPath>(arguments.value(0)).path();
                calls << "RemoveDevice " + removed;
                if (!m_objects.contains(removed))
                    return error("org.bluez.Error.DoesNotExist");
                const auto interfaces = m_objects.take(removed).keys();
                auto signal = QDBusMessage::createSignal("/", "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved");
                signal << QVariant::fromValue(QDBusObjectPath(removed)) << QStringList(interfaces);
                m_bus.send(signal);
                return reply();
            }
        }
        if (interface == device && m_objects.value(path).contains(device))
        {
            calls << member + " " + path;
            if (member == "Connect")
            {
                if (failConnect.contains(path))
                    return error("org.bluez.Error.Failed");
                change(path, device, "Connected", true);
                return reply();
            }
            if (member == "Disconnect")
            {
                change(path, device, "Connected", false);
                return reply();
            }
            if (member == "Pair")
            {
                // Answered by finishPair, after the agent's questions.
                pendingPair = message;
                return true;
            }
            if (member == "CancelPairing")
            {
                finishPair(false, "org.bluez.Error.AuthenticationCanceled");
                return reply();
            }
        }
        return false;
    }

  private:
    QDBusConnection m_bus;
    QMap<QString, BluetoothInterfaces> m_objects;
};
