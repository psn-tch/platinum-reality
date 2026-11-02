#include "energy_settings.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
bool valid(const QString &key, int minutes)
{
    if (minutes == 0)
        return key == "displayOff" || key == "sleep";
    return key == "displayOff" ? minutes >= 1 && minutes <= 240 : key == "sleep" && minutes >= 5 && minutes <= 480;
}
} // namespace

EnergySettings::EnergySettings(QString profile, QObject *parent)
    : QObject(parent), m_profile(QDir(profile).absolutePath())
{
    QFile input(m_profile + "/energy.json");
    if (QFileInfo(input.fileName()).isSymLink() || !input.open(QIODevice::ReadOnly) || input.size() > 1024)
        return;
    const auto object = QJsonDocument::fromJson(input.read(1025)).object();
    if (object.value("version").toInt() != 1)
        return;
    for (const auto &[key, value] : {std::pair{"displayOff", &m_displayOff}, std::pair{"sleep", &m_sleep}})
    {
        const auto stored = object.value(key);
        if (stored.isDouble() && stored.toDouble() == stored.toInt() && valid(key, stored.toInt()))
            *value = stored.toInt();
    }
}
bool EnergySettings::set(const QString &key, int minutes)
{
    if (!valid(key, minutes))
    {
        emit errorOccurred("That Energy Saver setting is outside its supported range.");
        return false;
    }
    auto displayOff = m_displayOff, sleep = m_sleep;
    (key == "sleep" ? sleep : displayOff) = minutes;
    if (displayOff == m_displayOff && sleep == m_sleep)
        return true;
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"displayOff", displayOff}, {"sleep", sleep}}).toJson();
    QSaveFile file(m_profile + "/energy.json");
    if (!QDir().mkpath(m_profile) || QFileInfo(file.fileName()).isSymLink() || !file.open(QIODevice::WriteOnly) ||
        file.write(bytes) != bytes.size() || !file.commit())
    {
        emit errorOccurred("Energy Saver settings could not be saved.");
        return false;
    }
    m_displayOff = displayOff;
    m_sleep = sleep;
    emit changed();
    emit saved();
    return true;
}
