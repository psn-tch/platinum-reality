#include "software_update.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>

SoftwareUpdate::SoftwareUpdate(QString client, QString keyring, QString settingsFile, QObject *parent)
    : QObject(parent), m_client(std::move(client)), m_keyring(std::move(keyring)), m_settings(std::move(settingsFile))
{
    QFile file(m_settings);
    if (file.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(file.read(65536)).object();
        m_automatic = object.value("automatic").toBool();
        m_lastChecked = QDateTime::fromString(object.value("lastChecked").toString(), Qt::ISODate);
    }
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_buffer += m_process.readAllStandardOutput();
        for (int end; (end = m_buffer.indexOf('\n')) >= 0;) {
            const auto line = m_buffer.left(end);
            m_buffer.remove(0, end + 1);
            const auto document = QJsonDocument::fromJson(line);
            if (document.isObject()) handle(document.object());
        }
        if (m_buffer.size() > 1 << 20) m_buffer.clear();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_phase.clear(); m_error = "Software Update could not start."; emit changed();
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const auto finished = m_phase;
        m_phase.clear(); m_buffer.clear();
        if ((status != QProcess::NormalExit || code != 0) && m_error.isEmpty())
            m_error = "Software Update stopped unexpectedly.";
        if (finished == "checking" && m_error.isEmpty()) {
            m_checked = true; m_lastChecked = QDateTime::currentDateTimeUtc(); save();
            if (m_silent && !m_available.isEmpty())
                emit updateFound(m_available.value("version").toString());
        }
        if (m_silent) { m_error.clear(); m_silent = false; }
        if ((finished == "installing" || finished == "restoring") && m_error.isEmpty()) {
            m_ready.clear(); m_readyRelease.clear(); m_available.clear();
            refresh();
        }
        emit changed();
    });
    // Automatic checks: when on, once a week, looked at hourly so a sleeping computer catches up.
    m_schedule.setInterval(60 * 60 * 1000);
    connect(&m_schedule, &QTimer::timeout, this, [this] {
        if (!m_automatic || busy()) return;
        if (m_lastChecked.isValid() && m_lastChecked.secsTo(QDateTime::currentDateTimeUtc()) < 7 * 24 * 3600) return;
        m_silent = true;
        check();
    });
    m_schedule.start();
}

SoftwareUpdate::~SoftwareUpdate()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

void SoftwareUpdate::run(const QString &phase, const QStringList &arguments)
{
    if (busy()) return;
    if (!QFileInfo(m_client).isFile()) {
        m_error = "Software Update is not installed."; emit changed(); return;
    }
    m_phase = phase; m_error.clear(); m_message.clear(); m_progress = 0;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONPATH"); environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    m_process.setProcessEnvironment(environment);
    m_process.setStandardErrorFile(QProcess::nullDevice());
    m_process.start("/usr/bin/python3", QStringList{"-I", m_client} + arguments + QStringList{"--keyring", m_keyring});
    emit changed();
}

void SoftwareUpdate::handle(const QJsonObject &message)
{
    if (message.contains("error")) m_error = message.value("error").toString();
    if (message.contains("installed")) m_installed = message.value("installed").toString();
    if (message.contains("previous")) m_previousKept = message.value("previous").toBool();
    if (m_phase == "checking" && message.contains("available"))
        m_available = message.value("available").toObject().toVariantMap();
    if (message.contains("bundles")) m_bundles = message.value("bundles").toArray().toVariantList();
    if (message.contains("ready")) {
        m_ready = message.value("ready").toString();
        m_readyRelease = message.value("release").toObject().toVariantMap();
    }
    if (message.contains("progress")) {
        const double total = message.value("total").toDouble();
        m_progress = total > 0 ? qBound(0.0, message.value("progress").toDouble() / total, 1.0) : 0;
    }
    if (message.value("step").toString() == "verify") m_message = "Checking the update…";
    if (message.value("step").toString() == "install") m_message = "Installing…";
    if (message.value("done").toBool())
        m_message = "Platinum Reality " + message.value("version").toString() +
                    " is installed. Log out and back in to use it.";
    emit changed();
}

void SoftwareUpdate::save() const
{
    QSaveFile file(m_settings);
    if (!file.open(QIODevice::WriteOnly)) return;
    QJsonObject object{{"automatic", m_automatic}};
    if (m_lastChecked.isValid()) object.insert("lastChecked", m_lastChecked.toString(Qt::ISODate));
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    file.commit();
}

void SoftwareUpdate::setAutomatic(bool on)
{
    if (on == m_automatic) return;
    m_automatic = on; save(); emit changed();
}

void SoftwareUpdate::refresh() { run("status", {"status"}); }
void SoftwareUpdate::check() { m_available.clear(); run("checking", {"check"}); }
void SoftwareUpdate::findBundles() { run("finding", {"find"}); }

void SoftwareUpdate::download()
{
    const auto tag = m_available.value("tag").toString();
    if (!tag.isEmpty()) run("downloading", {"download", tag});
}

void SoftwareUpdate::useLocal(const QString &folder)
{
    m_ready.clear(); m_readyRelease.clear();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        m_error = "Choose a folder holding a Platinum Reality release."; emit changed(); return;
    }
    run("verifying", {"verify", QFileInfo(folder).absoluteFilePath()});
}

void SoftwareUpdate::install()
{
    if (!m_ready.isEmpty()) run("installing", {"install", m_ready});
}

void SoftwareUpdate::restorePrevious() { run("restoring", {"restore"}); }
