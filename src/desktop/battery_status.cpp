#include "battery_status.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace
{
QString value(const QString &directory, const char *name)
{
    QFile file(directory + "/" + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(file.read(64)).trimmed();
}

qint64 number(const QString &directory, const char *name)
{
    bool ok = false;
    const auto parsed = value(directory, name).toLongLong(&ok);
    return ok ? parsed : -1;
}
} // namespace

BatteryStatus::BatteryStatus(QString root, QObject *parent) : QObject(parent), m_root(std::move(root))
{
    m_poll.setInterval(30000);
    connect(&m_poll, &QTimer::timeout, this, &BatteryStatus::refresh);
    m_poll.start();
    refresh();
}

void BatteryStatus::refresh()
{
    bool present = false, pluggedIn = false;
    int percent = -1, minutes = -1;
    QString state;
    // An empty root (tests, previews) means no battery, never the current directory.
    const auto supplies = m_root.isEmpty() ? QStringList()
                          : QDir(m_root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System, QDir::Name);
    for (const auto &name : supplies)
    {
        const auto directory = m_root + "/" + name;
        const auto type = value(directory, "type");
        if (type == "Mains" && number(directory, "online") == 1)
            pluggedIn = true;
        if (present || type != "Battery" || value(directory, "scope") == "Device" ||
            number(directory, "present") == 0)
            continue;
        const auto capacity = number(directory, "capacity");
        if (capacity < 0)
            continue;
        present = true;
        percent = int(std::clamp<qint64>(capacity, 0, 100));
        state = value(directory, "status");
        // Energy (µWh) and power (µW), or charge (µAh) and current (µA): the same ratio in hours.
        auto now = number(directory, "energy_now"), full = number(directory, "energy_full"),
             rate = number(directory, "power_now");
        if (now < 0 || rate < 0)
        {
            now = number(directory, "charge_now");
            full = number(directory, "charge_full");
            rate = number(directory, "current_now");
        }
        if (rate > 0 && now >= 0)
        {
            if (state == "Discharging")
                minutes = int(now * 60 / rate);
            else if (state == "Charging" && full > now)
                minutes = int((full - now) * 60 / rate);
            if (minutes > 48 * 60)
                minutes = -1; // A near-zero rate is not an estimate.
        }
    }
    if (present == m_present && percent == m_percent && state == m_state && pluggedIn == m_pluggedIn &&
        minutes == m_minutes)
        return;
    m_present = present;
    m_percent = percent;
    m_state = state;
    m_pluggedIn = pluggedIn;
    m_minutes = minutes;
    emit changed();
}
