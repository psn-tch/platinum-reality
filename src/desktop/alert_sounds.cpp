#include "alert_sounds.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QtEndian>
#include <cmath>
#include <unistd.h>

namespace
{
constexpr qint64 maximumBytes = 4 * 1024 * 1024;
constexpr int maximumSounds = 64;

// An original alert: a 1 kHz square tone with a quick decay, 22.05 kHz mono 16-bit PCM.
QByteArray simpleBeep()
{
    constexpr int rate = 22050, frames = rate * 12 / 100;
    QByteArray samples;
    samples.reserve(frames * 2);
    for (int i = 0; i < frames; ++i)
    {
        const double envelope = std::exp(-6.0 * i / frames) * std::min(1.0, i / 40.0);
        const qint16 value = qint16(((i * 1000 * 2 / rate) % 2 ? 1 : -1) * 9000 * envelope);
        char bytes[2];
        qToLittleEndian(value, bytes);
        samples.append(bytes, 2);
    }
    QByteArray wav("RIFF");
    const auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); wav.append(b, 4); };
    const auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); wav.append(b, 2); };
    u32(36 + samples.size());
    wav.append("WAVEfmt ");
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    wav.append("data");
    u32(samples.size());
    return wav + samples;
}

bool validWave(const QFileInfo &file)
{
    if (!file.isFile() || file.size() < 44 || file.size() > maximumBytes)
        return false;
    QFile wave(file.absoluteFilePath());
    if (!wave.open(QIODevice::ReadOnly))
        return false;
    const auto header = wave.read(12);
    return header.startsWith("RIFF") && header.mid(8, 4) == "WAVE";
}
} // namespace

AlertSounds::AlertSounds(QString profile, QString assets, QString player, QObject *parent)
    : QObject(parent), m_profile(std::move(profile)), m_assets(std::move(assets)), m_player(std::move(player))
{
    m_limit.setSingleShot(true);
    m_limit.setInterval(5000);
    connect(&m_limit, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, &QProcess::finished, &m_limit, &QTimer::stop);
    QFile saved(m_profile + "/alert-sound.json");
    if (!QFileInfo(saved.fileName()).isSymLink() && saved.size() <= 4096 && saved.open(QIODevice::ReadOnly))
    {
        const auto object = QJsonDocument::fromJson(saved.readAll()).object();
        if (object.value("sound").isString())
            m_selected = object.value("sound").toString().left(120);
        if (object.value("volume").isDouble())
            m_volume = std::clamp(object.value("volume").toInt(), 0, 100);
    }
    refresh();
}

AlertSounds::~AlertSounds()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning)
    {
        m_process.kill();
        m_process.waitForFinished(500);
    }
}

void AlertSounds::refresh()
{
    // The original sound lives in the profile, rewritten only when it differs.
    const auto beep = m_profile + "/sounds/Simple Beep.wav";
    const auto wave = simpleBeep();
    QFile current(beep);
    if (!current.open(QIODevice::ReadOnly) || current.readAll() != wave)
    {
        QDir().mkpath(m_profile + "/sounds");
        QSaveFile file(beep);
        if (file.open(QIODevice::WriteOnly) && file.write(wave) == wave.size())
            file.commit();
    }
    QVariantList sounds{QVariantMap{{"name", "Simple Beep"}, {"path", beep}, {"personal", false}}};
    const auto personal = QDir(assetFolder()).entryInfoList({"*.wav", "*.WAV", "*.Wav"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const auto &file : personal)
        if (sounds.size() <= maximumSounds && file.completeBaseName() != "Simple Beep" && validWave(file))
            sounds.append(QVariantMap{{"name", file.completeBaseName().left(120)},
                                      {"path", file.absoluteFilePath()}, {"personal", true}});
    const bool changedSounds = sounds != m_sounds;
    m_sounds = sounds;
    if (pathOf(m_selected).isEmpty())
        m_selected = "Simple Beep";
    if (changedSounds)
        emit changed();
}

QString AlertSounds::pathOf(const QString &name) const
{
    for (const auto &sound : m_sounds)
        if (sound.toMap().value("name").toString() == name)
            return sound.toMap().value("path").toString();
    return {};
}

void AlertSounds::save() const
{
    QDir().mkpath(m_profile);
    QSaveFile file(m_profile + "/alert-sound.json");
    if (file.open(QIODevice::WriteOnly) &&
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"sound", m_selected}, {"volume", m_volume}}).toJson()) > 0)
        file.commit();
}

void AlertSounds::choose(const QString &name)
{
    if (pathOf(name).isEmpty())
        return;
    if (m_selected != name)
    {
        m_selected = name;
        save();
        emit changed();
    }
    play();
}

void AlertSounds::setVolume(int value)
{
    value = std::clamp(value, 0, 100);
    if (value == m_volume)
        return;
    m_volume = value;
    save();
    emit changed();
}

void AlertSounds::play()
{
    const auto path = pathOf(m_selected);
    if (m_volume == 0 || m_player.isEmpty() || path.isEmpty() || !QFileInfo(m_player).isExecutable())
    {
        emit flash();
        return;
    }
    // One alert at a time; a burst of alerts is one sound.
    if (m_process.state() != QProcess::NotRunning)
        return;
    auto environment = QProcessEnvironment::systemEnvironment();
    // The desktop's runtime directory is private; use the user's own audio service.
    const auto runtime = QString("/run/user/%1").arg(getuid());
    if (!environment.contains("PIPEWIRE_RUNTIME_DIR") && QFileInfo(runtime + "/pipewire-0").exists())
        environment.insert("PIPEWIRE_RUNTIME_DIR", runtime);
    m_process.setProcessEnvironment(environment);
    m_process.setStandardOutputFile(QProcess::nullDevice());
    m_process.setStandardErrorFile(QProcess::nullDevice());
    m_process.start(m_player, {"--volume", QString::number(m_volume / 100.0, 'f', 2), path});
    m_limit.start();
    emit played(path, m_volume);
}
