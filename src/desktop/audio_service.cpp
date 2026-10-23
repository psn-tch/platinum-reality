#include "audio_service.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <csignal>

AudioService::AudioService(QString helper, QObject *parent, int timeout)
    : QObject(parent), m_helper(std::move(helper))
{
    // Keep this helper and its ordinary pactl subprocesses in an owned group.
    // Stopping only Python can leave the currently running command behind.
    m_process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(timeout);
    connect(&m_timeout, &QTimer::timeout, this, [this] { m_timedOut = true; stopHelper(); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AudioService::collectReply);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_timeout.stop(); m_busy = false;
            fail("Sound controls could not start.");
            emit busyChanged();
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_timeout.stop(); m_busy = false;
        collectReply();
        const auto document = m_oversized ? QJsonDocument{} : QJsonDocument::fromJson(m_reply);
        const auto object = document.object();
        const auto reported = object.value("state").toObject();
        const bool stateValid = reported.value("outputs").isArray() && reported.value("inputs").isArray() &&
                                reported.value("cards").isArray() && reported.value("defaultOutput").isString() &&
                                reported.value("defaultInput").isString();
        if (m_oversized) {
            fail("The sound service returned too much information.");
        } else if (m_timedOut || status != QProcess::NormalExit || !document.isObject()) {
            fail("Sound controls did not respond.");
        } else if (!object.value("error").toString().isEmpty()) {
            if (stateValid) m_state = reported.toVariantMap();
            fail(object.value("error").toString());
        } else if (code != 0 || !stateValid) {
            fail("The sound device list could not be read.");
        } else {
            m_state = reported.toVariantMap();
            m_warning = object.value("warning").toString();
            const auto selected = m_requested.constFind(m_active.key);
            if (selected != m_requested.cend() && selected->arguments == m_active.arguments)
                m_requested.remove(m_active.key);
            notifyState(); emit errorChanged();
        }
        m_active = {};
        emit busyChanged();
        next();
    });
}

AudioService::~AudioService()
{
    m_timeout.stop();
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        stopHelper();
        m_process.waitForFinished(1000);
    }
}

void AudioService::stopHelper()
{
    // A QProcess that has not been reaped still owns this PID. The child group
    // was created by Qt's CreateNewSession before it exec'd the Python helper.
    const auto pid = m_process.processId();
    if (m_process.state() != QProcess::NotRunning && pid > 0)
        ::kill(-pid, SIGKILL);
    m_process.kill();
}

void AudioService::collectReply()
{
    if (m_oversized) return;
    constexpr qsizetype maximum = 2 * 1024 * 1024;
    m_reply += m_process.read(maximum + 1 - m_reply.size());
    if (m_reply.size() > maximum) {
        m_oversized = true;
        m_reply.clear();
        stopHelper();
        m_process.closeReadChannel(QProcess::StandardOutput);
    }
}

bool AudioService::validName(const QString &name)
{
    static const QRegularExpression pattern("^[A-Za-z0-9_.:+][A-Za-z0-9_.:+-]{0,511}$");
    return pattern.match(name).hasMatch();
}

void AudioService::fail(const QString &message)
{
    m_error = message;
    m_warning.clear();
    m_pending.clear(); m_requested.clear(); m_refreshPending = false;
    notifyState(); emit errorChanged();
}

void AudioService::notifyState()
{
    if (m_stateQueued) return;
    m_stateQueued = true;
    // A tab/device binding may commit a pending slider edit. Do not re-enter
    // that binding through stateChanged or start a request inside a reply's
    // state notification before its bookkeeping has finished.
    QMetaObject::invokeMethod(this, [this] {
        m_stateQueued = false;
        emit stateChanged();
    }, Qt::QueuedConnection);
}

QVariantMap AudioService::state() const
{
    auto result = m_state;
    for (const auto &request : m_requested) {
        const auto &args = request.arguments;
        if (args.first() == "output" || args.first() == "input") {
            result.insert(args.first() == "output" ? "defaultOutput" : "defaultInput", args.at(1));
        } else {
            const bool profile = args.first() == "profile";
            const auto collection = profile ? "cards" : args.at(1) == "output" ? "outputs" : "inputs";
            auto rows = result.value(collection).toList();
            for (auto &row : rows) {
                auto value = row.toMap();
                if (value.value("name").toString() != args.at(profile ? 1 : 2)) continue;
                if (profile) value.insert("activeProfile", args.at(2));
                else if (args.first() == "volume") value.insert("volume", args.at(3).toInt());
                else value.insert("muted", bool(args.at(3).toInt()));
                row = value;
            }
            result.insert(collection, rows);
        }
    }
    return result;
}

void AudioService::enqueue(Request request)
{
    m_requested.insert(request.key, request);
    notifyState();
    if (!m_busy) {
        start(std::move(request));
        return;
    }
    for (auto &pending : m_pending)
        if (pending.key == request.key) { pending = std::move(request); return; }
    m_pending.append(std::move(request));
}

void AudioService::start(Request request)
{
    if (!QFileInfo(m_helper).isFile() || !QFileInfo(m_helper).isAbsolute()) {
        fail("Sound controls are unavailable.");
        return;
    }
    m_active = std::move(request);
    m_error.clear(); m_warning.clear(); m_timedOut = false;
    m_oversized = false; m_reply.clear();
    m_busy = true;
    emit busyChanged(); emit errorChanged();
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONHOME"); environment.remove("PYTHONPATH");
    m_process.setProcessEnvironment(environment);
    m_process.setStandardErrorFile(QProcess::nullDevice());
    m_process.start("/usr/bin/python3", QStringList{"-I", m_helper} + m_active.arguments);
    m_timeout.start();
}

void AudioService::next()
{
    if (m_busy) return;
    if (!m_pending.isEmpty()) start(m_pending.takeFirst());
    else if (m_refreshPending) { m_refreshPending = false; refresh(); }
}

void AudioService::refresh()
{
    if (m_busy) { m_refreshPending = true; return; }
    start({{}, {"status"}});
}

void AudioService::chooseOutput(const QString &name)
{
    if (!validName(name)) { fail("Invalid sound output."); return; }
    enqueue({"output", {"output", name}});
}

void AudioService::chooseInput(const QString &name)
{
    if (!validName(name)) { fail("Invalid sound input."); return; }
    enqueue({"input", {"input", name}});
}

void AudioService::chooseProfile(const QString &card, const QString &profile)
{
    if (!validName(card) || !validName(profile)) { fail("Invalid sound profile."); return; }
    enqueue({"profile:" + card, {"profile", card, profile}});
}

void AudioService::setVolume(const QString &kind, const QString &name, int value)
{
    if ((kind != "output" && kind != "input") || !validName(name) || value < 0 || value > 100) {
        fail("Invalid sound level."); return;
    }
    enqueue({"volume:" + kind + ":" + name, {"volume", kind, name, QString::number(value)}});
}

void AudioService::setMuted(const QString &kind, const QString &name, bool muted)
{
    if ((kind != "output" && kind != "input") || !validName(name)) { fail("Invalid sound device."); return; }
    enqueue({"mute:" + kind + ":" + name, {"mute", kind, name, muted ? "1" : "0"}});
}
