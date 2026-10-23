#include "system_controls.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

SystemControls::SystemControls(QString helper, QObject *parent)
    : QObject(parent), m_helper(std::move(helper))
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(25000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        m_process.kill();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_timeout.stop(); m_busy = false;
        m_error = "The system controls helper could not start.";
        m_pending.clear(); m_requested.clear(); m_refreshPending=false;
        emit stateChanged(); emit busyChanged(); emit errorChanged();
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop(); m_busy = false;
        const auto bytes = m_process.readAllStandardOutput();
        const auto document = bytes.size() <= 65536 ? QJsonDocument::fromJson(bytes) : QJsonDocument{};
        const auto object = document.object();
        if (m_timedOut || status != QProcess::NormalExit || !document.isObject())
            m_error = "The system controls service did not respond.";
        else {
            m_error = object.value("error").toString();
            if (object.value("state").isObject()) m_state = object.value("state").toObject().toVariantMap();
            if (code != 0 && m_error.isEmpty()) m_error = "The setting could not be changed.";
        }
        if (m_error.isEmpty()) {
            if (m_requested.contains(m_activeControl) && m_requested.value(m_activeControl)==m_activeValue)
                m_requested.remove(m_activeControl);
        } else {
            // Do not keep retrying queued changes after a denied/failed action.
            m_pending.clear(); m_requested.clear(); m_refreshPending=false;
        }
        m_activeControl.clear();
        emit stateChanged(); emit busyChanged(); emit errorChanged();
        startPending();
    });
}
SystemControls::~SystemControls()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
void SystemControls::start(const QStringList &arguments)
{
    if (m_busy) return;
    m_error.clear(); m_timedOut = false;
    if (!QFileInfo(m_helper).isFile() || !QFileInfo(m_helper).isAbsolute()) {
        m_error = "The system controls helper is unavailable.";
        m_pending.clear(); m_requested.clear(); m_refreshPending=false;
        emit stateChanged(); emit errorChanged(); return;
    }
    m_activeControl=arguments.first()=="set" ? arguments.value(1) : QString();
    m_activeValue=arguments.value(2).toInt();
    m_busy = true; emit busyChanged(); emit errorChanged();
    m_process.setStandardErrorFile(QProcess::nullDevice());
    auto environment = QProcessEnvironment::systemEnvironment();
    // Do not import code from an inherited Python search path.
    environment.remove("PYTHONPATH"); environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    m_process.setProcessEnvironment(environment);
    m_process.start("/usr/bin/python3", QStringList{"-I", m_helper} + arguments);
    m_timeout.start();
}
QVariantMap SystemControls::state() const
{
    auto displayed=m_state;
    for (auto it=m_requested.cbegin();it!=m_requested.cend();++it) {
        const auto name=it.key()=="mute" ? QString("volume") : it.key();
        auto control=displayed.value(name).toMap();
        if (!control.value("available").toBool()) continue;
        const auto field=it.key()=="mute" ? "muted" : (name=="volume" || name=="brightness") ? "value" : "enabled";
        control.insert(field,(it.key()=="volume" || it.key()=="brightness") ? QVariant(it.value()) : QVariant(bool(it.value())));
        displayed.insert(name,control);
    }
    return displayed;
}
void SystemControls::startPending()
{
    if (m_busy) return;
    if (!m_pending.isEmpty()) {
        const auto next = m_pending.takeFirst();
        start({"set", next.first, QString::number(next.second)});
    } else if (m_refreshPending) {
        m_refreshPending = false;
        start({"status"});
    }
}
void SystemControls::refresh()
{
    if (m_busy) { m_refreshPending = true; return; }
    start({"status"});
}
void SystemControls::set(const QString &control, int value)
{
    const bool percent = control == "volume" || control == "brightness";
    if ((!percent && control != "mute" && control != "wifi" && control != "bluetooth") ||
        value < 0 || value > (percent ? 100 : 1)) {
        m_error = "Invalid control value."; emit errorChanged(); return;
    }
    m_requested.insert(control,value);
    emit stateChanged();
    if (m_busy) {
        // A held arrow key or a quick second change must not be dropped: apply
        // only the newest value for each control once the running helper ends.
        for (auto &pending : m_pending)
            if (pending.first == control) { pending.second = value; return; }
        m_pending.append({control, value});
        return;
    }
    start({"set", control, QString::number(value)});
}
