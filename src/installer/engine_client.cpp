#include "engine_client.hpp"
#include <QDir>
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>

EngineClient::EngineClient(QString program, QObject *parent) : QObject(parent), m_program(std::move(program)) {}

QProcess *EngineClient::start(const QStringList &arguments)
{
    if (running())
        return nullptr;
    m_background = arguments.value(0) == "choices";
    if (m_process)
        m_process->deleteLater();
    m_process = new QProcess(this);
    m_process->setProgram(m_program);
    m_process->setArguments(arguments);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit failed("The installer could not start its disk tool (" + m_program + ").");
    });
    return m_process;
}

QString EngineClient::writeConfig(const QJsonObject &config)
{
    // A private file for the engine; it holds no passwords.
    const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const auto path = QDir(runtime.isEmpty() ? QDir::tempPath() : runtime).filePath("platinum-reality-install.json");
    QSaveFile file(path);
    const auto bytes = QJsonDocument(config).toJson();
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit())
        return {};
    return path;
}

bool EngineClient::afterCurrent(std::function<void()> again)
{
    if (!running())
        return false;
    connect(m_process, &QProcess::finished, this, [again] { again(); }, Qt::SingleShotConnection);
    return true;
}

void EngineClient::listDisks()
{
    if (afterCurrent([this] { listDisks(); }))
        return;
    auto *process = start({"disks", "--json"});
    if (!process)
        return;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const auto document = QJsonDocument::fromJson(process->readAllStandardOutput());
        if (status != QProcess::NormalExit || code != 0 || !document.isArray())
        {
            emit failed("The installer could not list the disks.");
            return;
        }
        QVariantList disks;
        for (const auto &value : document.array())
        {
            const auto disk = value.toObject();
            if (disk.value("path").toString().startsWith("/dev/"))
                disks.append(disk.toVariantMap());
        }
        emit disksListed(disks);
    });
    process->start();
}

void EngineClient::listChoices()
{
    auto *process = start({"choices", "--json"});
    if (!process)
    {
        emit choicesListed({});
        return;
    }
    // An engine without this command is not a failure: nothing is shown, the defaults stay.
    disconnect(process, &QProcess::errorOccurred, this, nullptr);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emit choicesListed({});
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const auto document = QJsonDocument::fromJson(process->readAllStandardOutput());
        emit choicesListed(status == QProcess::NormalExit && code == 0 && document.isObject() ? document.object()
                                                                                                 : QJsonObject{});
    });
    process->start();
}

void EngineClient::plan(const QJsonObject &config)
{
    if (afterCurrent([this, config] { plan(config); }))
        return;
    m_configPath = writeConfig(config);
    if (m_configPath.isEmpty())
    {
        emit failed("The installer could not save its settings.");
        return;
    }
    auto *process = start({"plan", "--config", m_configPath, "--json"});
    if (!process)
        return;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        const auto document = QJsonDocument::fromJson(process->readAllStandardOutput());
        const auto plan = document.object();
        if (status != QProcess::NormalExit || code != 0 || !document.isObject() || !plan.value("ok").toBool())
        {
            const auto warnings = plan.value("warnings").toArray();
            emit failed(warnings.isEmpty() ? QString("The installer could not prepare the installation.")
                                           : warnings.first().toString());
            return;
        }
        emit planned(plan);
    });
    process->start();
}

void EngineClient::install(const QJsonObject &config, const QJsonObject &secrets, const QString &confirmDisk)
{
    if (afterCurrent([this, config, secrets, confirmDisk] { install(config, secrets, confirmDisk); }))
        return;
    m_configPath = writeConfig(config);
    if (m_configPath.isEmpty())
    {
        emit failed("The installer could not save its settings.");
        return;
    }
    auto *process = start({"install", "--config", m_configPath, "--confirm-erase", confirmDisk, "--progress-fd", "1"});
    if (!process)
        return;
    m_pending.clear();
    m_lastStep.clear();
    m_finished = false;
    connect(process, &QProcess::readyReadStandardOutput, this, &EngineClient::readProgress);
    connect(process, &QProcess::started, this, [process, secrets] {
        process->write(QJsonDocument(secrets).toJson(QJsonDocument::Compact) + "\n");
        process->closeWriteChannel();
    });
    connect(process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readProgress();
        if (m_finished)
            return;
        m_finished = true;
        emit installFinished(false, status == QProcess::NormalExit && code == 0
                                        ? QString("The installer stopped without reporting a result.")
                                        : QString("The installation stopped during “%1”.").arg(m_lastStep));
    });
    process->start();
}

void EngineClient::readProgress()
{
    if (!m_process)
        return;
    m_pending += m_process->readAllStandardOutput();
    qsizetype newline;
    while ((newline = m_pending.indexOf('\n')) >= 0)
    {
        const auto line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        const auto event = QJsonDocument::fromJson(line).object();
        if (event.contains("result"))
        {
            if (m_finished)
                continue;
            m_finished = true;
            const bool ok = event.value("result").toString() == "ok";
            emit installFinished(ok, ok ? QString() : event.value("message").toString());
        }
        else if (event.contains("step"))
        {
            m_lastStep = event.value("message").toString(event.value("step").toString());
            emit progress(event.value("step").toString(), std::clamp(event.value("fraction").toDouble(), 0.0, 1.0),
                          event.value("message").toString());
        }
    }
}

void EngineClient::cancel()
{
    if (running())
        m_process->terminate();
}
