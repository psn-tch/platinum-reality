#include "system_sounds.hpp"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

SystemSounds::SystemSounds(QString helper, QStringList elevate, QObject *parent)
    : QObject(parent), m_helper(std::move(helper)), m_elevate(std::move(elevate))
{
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONPATH");
    environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    for (auto *process : {&m_status, &m_change})
    {
        process->setProcessEnvironment(environment);
        process->setStandardErrorFile(QProcess::nullDevice());
        connect(process, &QProcess::finished, this,
                [this, process](int code, QProcess::ExitStatus status) { finished(process, code, status); });
    }
}

SystemSounds::~SystemSounds()
{
    for (auto *process : {&m_status, &m_change})
        if (process->state() != QProcess::NotRunning)
        {
            process->disconnect(this);
            process->kill();
            process->waitForFinished(2000);
        }
}

bool SystemSounds::available() const
{
    return QFileInfo(m_helper).isFile();
}

void SystemSounds::finished(QProcess *process, int code, QProcess::ExitStatus status)
{
    bool reported = false;
    for (const auto &line : process->readAllStandardOutput().split('\n'))
    {
        const auto message = QJsonDocument::fromJson(line).object();
        if (message.isEmpty())
            continue;
        reported = true;
        if (message.contains("error"))
            m_error = message.value("error").toString();
        if (message.contains("removed"))
        {
            m_removed = message.value("removed").toBool();
            m_present.clear();
            for (const auto &name : message.value("present").toArray())
                m_present.append(name.toString());
        }
    }
    if (process == &m_change)
    {
        // pkexec: 126 is a dismissed password box (nothing to say), 127 no way to ask at all.
        if (!reported && status == QProcess::NormalExit && code == 127)
            m_error = tr("No password box could be shown, so nothing was changed.");
        else if (!reported && (status != QProcess::NormalExit || code != 126))
            m_error = tr("The desktop sounds could not be changed.");
    }
    emit changed();
}

void SystemSounds::refresh()
{
    if (m_status.state() != QProcess::NotRunning || changing() || !available())
        return;
    m_status.start("/usr/bin/python3", {"-I", m_helper, "status"});
}

void SystemSounds::setRemoved(bool removed)
{
    if (changing() || !available())
        return;
    m_error.clear();
    const QString argument = removed ? "remove" : "restore";
    if (m_elevate.isEmpty())
        m_change.start("/usr/bin/python3", {"-I", m_helper, argument});
    else
        m_change.start(m_elevate.first(), m_elevate.mid(1) + QStringList{m_helper, argument});
    emit changed();
}
