#include "interface_sounds.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <unistd.h>

namespace
{
constexpr qint64 maximumBytes = 4 * 1024 * 1024;
// Theme sounds are short; a burst (closing many windows) plays the first few, not a queue.
constexpr std::size_t maximumPlaying = 3;

bool validWave(const QFileInfo &file)
{
    if (!file.isFile() || file.isSymLink() || file.size() < 44 || file.size() > maximumBytes)
        return false;
    QFile wave(file.absoluteFilePath());
    if (!wave.open(QIODevice::ReadOnly))
        return false;
    const auto header = wave.read(12);
    return header.startsWith("RIFF") && header.mid(8, 4) == "WAVE";
}
} // namespace

InterfaceSounds::InterfaceSounds(QString profile, QString assets, QString player, std::function<int()> volume,
                                 QObject *parent)
    : QObject(parent), m_profile(std::move(profile)), m_folder(assets + "/sounds/platinum"),
      m_player(std::move(player)), m_volume(std::move(volume))
{
    QFile saved(m_profile + "/interface-sounds.json");
    if (!QFileInfo(saved.fileName()).isSymLink() && saved.size() <= 4096 && saved.open(QIODevice::ReadOnly))
        m_enabled = QJsonDocument::fromJson(saved.readAll()).object().value("track").toString() != "None";
    refresh();
}

InterfaceSounds::~InterfaceSounds()
{
    for (auto *process : m_playing)
    {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(200);
    }
}

void InterfaceSounds::refresh()
{
    // manifest.json: {"<id>": {"file": "<id>.wav", "tags": "chke/lare/sbae/dsce"}}. A part with
    // a qualifier ("wmov moving") is only reachable by its full text.
    QHash<QString, QString> files;
    QFile manifest(m_folder + "/manifest.json");
    if (!QFileInfo(manifest.fileName()).isSymLink() && manifest.size() <= 65536 && manifest.open(QIODevice::ReadOnly))
    {
        static const QRegularExpression fileName("^-?[0-9]{1,6}\\.wav$");
        const auto object = QJsonDocument::fromJson(manifest.readAll()).object();
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            const auto entry = it.value().toObject();
            const auto file = entry.value("file").toString();
            if (!fileName.match(file).hasMatch() || !validWave(QFileInfo(m_folder + "/" + file)))
                continue;
            for (const auto &tag : entry.value("tags").toString().left(200).split('/', Qt::SkipEmptyParts))
                if (!files.contains(tag.trimmed()))
                    files.insert(tag.trimmed(), m_folder + "/" + file);
        }
    }
    if (files != m_files)
    {
        m_files = files;
        emit changed();
    }
}

void InterfaceSounds::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    QDir().mkpath(m_profile);
    QSaveFile file(m_profile + "/interface-sounds.json");
    if (file.open(QIODevice::WriteOnly) &&
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"track", enabled ? "Platinum Sounds" : "None"}})
                       .toJson()) > 0)
        file.commit();
    emit changed();
}

void InterfaceSounds::play(const QString &tag)
{
    const auto path = m_files.value(tag);
    const int volume = m_volume ? m_volume() : 0;
    if (!m_enabled || path.isEmpty() || volume <= 0)
        return;
    emit played(tag, path, volume);
    if (m_player.isEmpty() || !QFileInfo(m_player).isExecutable() || m_playing.size() >= maximumPlaying)
        return;
    auto *process = new QProcess(this);
    auto environment = QProcessEnvironment::systemEnvironment();
    // The desktop's runtime directory is private; use the user's own audio service.
    const auto runtime = QString("/run/user/%1").arg(getuid());
    if (!environment.contains("PIPEWIRE_RUNTIME_DIR") && QFileInfo(runtime + "/pipewire-0").exists())
        environment.insert("PIPEWIRE_RUNTIME_DIR", runtime);
    process->setProcessEnvironment(environment);
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    m_playing.push_back(process);
    const auto finish = [this, process] {
        std::erase(m_playing, process);
        process->deleteLater();
    };
    connect(process, &QProcess::finished, this, finish);
    connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish();
    });
    QTimer::singleShot(4000, process, &QProcess::kill);
    process->start(m_player, {"--volume", QString::number(volume / 100.0, 'f', 2), path});
}
