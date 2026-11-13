#include "camera_switch.hpp"
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

CameraSwitch::CameraSwitch(QString helper, QStringList elevate, QObject *parent)
    : QObject(parent), m_helper(std::move(helper)), m_elevate(std::move(elevate))
{
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONPATH");
    environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    for (auto *process : {&m_status, &m_switch})
    {
        process->setProcessEnvironment(environment);
        process->setStandardErrorFile(QProcess::nullDevice());
        connect(process, &QProcess::finished, this,
                [this, process](int code, QProcess::ExitStatus status) { finished(process, code, status); });
    }
}

CameraSwitch::~CameraSwitch()
{
    for (auto *process : {&m_status, &m_switch})
        if (process->state() != QProcess::NotRunning)
        {
            process->disconnect(this);
            process->kill();
            process->waitForFinished(2000);
        }
}

bool CameraSwitch::available() const
{
    return QFileInfo(m_helper).isFile();
}

void CameraSwitch::finished(QProcess *process, int code, QProcess::ExitStatus status)
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
        if (message.contains("off"))
        {
            m_off = message.value("off").toBool();
            m_cameras = message.value("cameras").toArray().toVariantList();
            m_others.clear();
            for (const auto &name : message.value("others").toArray())
                m_others.append(name.toString());
        }
    }
    if (process == &m_switch)
    {
        // pkexec: 126 is a dismissed password box (nothing to say), 127 no way to ask at all.
        if (!reported && status == QProcess::NormalExit && code == 127)
            m_error = tr("No password box could be shown, so the camera was not switched.");
        else if (!reported && (status != QProcess::NormalExit || code != 126))
            m_error = tr("The camera could not be switched.");
    }
    emit changed();
}

void CameraSwitch::refresh()
{
    if (m_status.state() != QProcess::NotRunning || changing() || !available())
        return;
    m_status.start("/usr/bin/python3", {"-I", m_helper, "status"});
}

void CameraSwitch::setOff(bool off)
{
    if (changing() || !available())
        return;
    m_error.clear();
    const QString argument = off ? "off" : "on";
    if (m_elevate.isEmpty())
        m_switch.start("/usr/bin/python3", {"-I", m_helper, argument});
    else
        m_switch.start(m_elevate.first(), m_elevate.mid(1) + QStringList{m_helper, argument});
    emit changed();
}
