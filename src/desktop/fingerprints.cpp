#include "fingerprints.hpp"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const auto service = QStringLiteral("net.reactivated.Fprint");
const auto managerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const auto managerInterface = QStringLiteral("net.reactivated.Fprint.Manager");
const auto deviceInterface = QStringLiteral("net.reactivated.Fprint.Device");
const auto errorPrefix = QStringLiteral("net.reactivated.Fprint.Error.");
} // namespace

FingerprintService::FingerprintService(QDBusConnection bus, QString helper, QStringList elevate, QObject *parent)
    : QObject(parent), m_bus(std::move(bus)), m_helperPath(std::move(helper)), m_elevate(std::move(elevate))
{
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PYTHONPATH");
    environment.remove("PYTHONHOME");
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    // Reading the status and changing it are separate runs: a quick status read never holds up,
    // or looks like, a change waiting for the administrator's password.
    for (auto *process : {&m_status, &m_helper})
    {
        process->setProcessEnvironment(environment);
        process->setStandardErrorFile(QProcess::nullDevice());
        connect(process, &QProcess::finished, this,
                [this, process](int code, QProcess::ExitStatus status) { helperFinished(process, code, status); });
    }
}

void FingerprintService::helperFinished(QProcess *process, int code, QProcess::ExitStatus status)
{
    bool reported = false, done = false;
    for (const auto &line : process->readAllStandardOutput().split('\n'))
    {
        const auto message = QJsonDocument::fromJson(line).object();
        if (message.isEmpty())
            continue;
        reported = true;
        if (message.contains("error"))
            m_error = message.value("error").toString();
        if (message.contains("available"))
        {
            m_loginAvailable = message.value("available").toBool();
            m_login = message.value("login").toBool();
            m_sudo = message.value("sudo").toBool();
        }
        done = done || message.value("done").toBool();
    }
    if (process == &m_helper)
    {
        m_message.clear();
        // pkexec: 126 is a dismissed password box (nothing to say), 127 no way to ask at all.
        if (!reported && status == QProcess::NormalExit && code == 127)
            m_error = tr("No password box could be shown, so nothing was changed.");
        else if (!reported && (status != QProcess::NormalExit || code != 126))
            m_error = tr("Fingerprint login could not be changed.");
    }
    emit changed();
    if (done) // fprintd may have just been installed: look for the reader again.
        refresh();
}

FingerprintService::~FingerprintService()
{
    if (m_claimed && !m_device.isEmpty())
    {
        // Give the reader back now, as the lock does: fprintd keeps a claim until its owner leaves.
        if (!m_enrolling.isEmpty())
            m_bus.call(QDBusMessage::createMethodCall(service, m_device, deviceInterface, "EnrollStop"),
                       QDBus::Block, 2000);
        m_bus.call(QDBusMessage::createMethodCall(service, m_device, deviceInterface, "Release"), QDBus::Block, 2000);
    }
    for (auto *process : {&m_status, &m_helper})
        if (process->state() != QProcess::NotRunning)
        {
            process->disconnect(this);
            process->kill();
            process->waitForFinished(2000);
        }
}

QStringList FingerprintService::allFingers()
{
    return {"right-index-finger", "right-middle-finger", "right-ring-finger", "right-little-finger", "right-thumb",
            "left-index-finger",  "left-middle-finger",  "left-ring-finger",  "left-little-finger",  "left-thumb"};
}

QString FingerprintService::fingerTitle(const QString &finger)
{
    QStringList words = finger.split('-', Qt::SkipEmptyParts);
    for (auto &word : words)
        word[0] = word[0].toUpper();
    return words.join(' ');
}

void FingerprintService::call(const QString &method, const QVariantList &arguments,
                              std::function<void(const QDBusMessage &)> done, bool interactive, const QString &path,
                              const QString &interface)
{
    auto message = QDBusMessage::createMethodCall(service, path.isEmpty() ? m_device : path,
                                                  interface.isEmpty() ? deviceInterface : interface, method);
    message.setArguments(arguments);
    // fprintd asks polkit; changing fingers may put the user's password box on screen.
    message.setInteractiveAuthorizationAllowed(interactive);
    ++m_pending;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, interactive ? 180000 : 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, done = std::move(done)]
            {
                watcher->deleteLater();
                --m_pending;
                done(watcher->reply());
                emit changed();
            });
    emit changed();
}

void FingerprintService::failed(const QDBusMessage &reply, const QString &doing)
{
    const auto name = reply.errorName();
    if (name == errorPrefix + "PermissionDenied" || name == "org.freedesktop.DBus.Error.AccessDenied")
        m_error = tr("%1: your password was not given.").arg(doing);
    else if (name == errorPrefix + "AlreadyInUse")
        m_error = tr("%1: the fingerprint reader is in use. Try again in a moment.").arg(doing);
    else if (name == errorPrefix + "NoSuchDevice")
        m_error = tr("%1: the fingerprint reader is not connected.").arg(doing);
    else
        m_error = tr("%1: the fingerprint reader did not answer (%2).").arg(doing, reply.errorMessage());
}

void FingerprintService::refresh()
{
    runHelper({"status"}, false);
    if (!m_enrolling.isEmpty() || !m_bus.isConnected())
    {
        emit changed();
        return;
    }
    const int generation = ++m_generation;
    call("GetDefaultDevice", {},
         [this, generation](const QDBusMessage &reply)
         {
             if (generation != m_generation)
                 return;
             if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
             {
                 // fprintd answers NoSuchDevice; with no fprintd the bus itself answers.
                 m_installed = reply.errorName().startsWith(errorPrefix);
                 m_device.clear();
                 m_reader.clear();
                 m_fingers.clear();
                 return;
             }
             m_installed = true;
             m_device = qvariant_cast<QDBusObjectPath>(reply.arguments().first()).path();
             call("GetAll", {deviceInterface},
                  [this, generation](const QDBusMessage &properties)
                  {
                      if (generation != m_generation || properties.arguments().isEmpty())
                          return;
                      const auto values = qdbus_cast<QVariantMap>(properties.arguments().first());
                      m_reader = values.value("name").toString();
                      if (m_reader.isEmpty())
                          m_reader = tr("Fingerprint reader");
                      m_scanType = values.value("scan-type").toString() == "swipe" ? "swipe" : "press";
                      m_stages = values.value("num-enroll-stages").toInt();
                  },
                  false, m_device, "org.freedesktop.DBus.Properties");
             // "" is the user calling.
             call("ListEnrolledFingers", {QString()},
                  [this, generation](const QDBusMessage &fingers)
                  {
                      if (generation != m_generation)
                          return;
                      QStringList found;
                      if (fingers.type() == QDBusMessage::ReplyMessage && !fingers.arguments().isEmpty())
                          found = fingers.arguments().first().toStringList();
                      // In the panel's order (fprintd's own order is the enrolment's).
                      QStringList ordered;
                      for (const auto &finger : allFingers())
                          if (found.contains(finger))
                              ordered << finger;
                      m_fingers = ordered;
                  });
         },
         false, managerPath, managerInterface);
}

void FingerprintService::claimed(std::function<void()> then)
{
    if (m_claimed)
        return then();
    call("Claim", {QString()},
         [this, then = std::move(then)](const QDBusMessage &reply)
         {
             if (reply.type() != QDBusMessage::ReplyMessage)
             {
                 failed(reply, m_enrolling.isEmpty() ? tr("The fingerprint was not deleted")
                                                     : tr("The fingerprint was not added"));
                 m_enrolling.clear();
                 return;
             }
             m_claimed = true;
             then();
         },
         true);
}

void FingerprintService::release()
{
    if (!m_claimed)
        return;
    m_claimed = false;
    m_bus.disconnect(service, m_device, deviceInterface, "EnrollStatus", this, SLOT(enrollStatus(QString, bool)));
    call("Release", {}, [](const QDBusMessage &) {});
}

void FingerprintService::enroll(const QString &finger)
{
    if (busy() || m_device.isEmpty() || !allFingers().contains(finger) || m_fingers.contains(finger))
        return;
    m_enrolling = finger;
    m_stage = 0;
    m_error.clear();
    m_message = tr("Waiting for the fingerprint reader…");
    claimed(
        [this, finger]
        {
            if (m_enrolling != finger) // Cancelled while the password box was up.
                return release();
            m_bus.connect(service, m_device, deviceInterface, "EnrollStatus", this, SLOT(enrollStatus(QString, bool)));
            call("EnrollStart", {finger},
                 [this, finger](const QDBusMessage &reply)
                 {
                     if (m_enrolling != finger)
                         return;
                     if (reply.type() != QDBusMessage::ReplyMessage)
                     {
                         failed(reply, tr("The fingerprint was not added"));
                         m_enrolling.clear();
                         m_message.clear();
                         return release();
                     }
                     m_message = m_scanType == "swipe" ? tr("Swipe your %1 across the reader.").arg(fingerTitle(finger).toLower())
                                                       : tr("Touch the reader with your %1.").arg(fingerTitle(finger).toLower());
                 },
                 true);
        });
    emit changed();
}

void FingerprintService::enrollStatus(const QString &result, bool done)
{
    if (m_enrolling.isEmpty())
        return;
    const bool swipe = m_scanType == "swipe";
    if (result == "enroll-completed")
    {
        m_stage = qMax(m_stage, m_stages);
        return finishEnroll({});
    }
    if (result == "enroll-stage-passed")
    {
        ++m_stage;
        m_message = swipe ? tr("Good. Swipe the same finger again.") : tr("Good. Lift your finger and touch the reader again.");
    }
    else if (result == "enroll-retry-scan" || result == "enroll-remove-and-retry")
        m_message = swipe ? tr("That didn't work. Swipe again.") : tr("That didn't work. Lift your finger and try again.");
    else if (result == "enroll-swipe-too-short")
        m_message = tr("Swipe a little more slowly, all the way across.");
    else if (result == "enroll-finger-not-centered")
        m_message = tr("Put your finger in the middle of the reader.");
    else if (result == "enroll-duplicate")
        return finishEnroll(tr("That fingerprint is already added."));
    else if (result == "enroll-data-full")
        return finishEnroll(tr("The reader has no room for another fingerprint."));
    else if (result == "enroll-disconnected")
        return finishEnroll(tr("The fingerprint reader was disconnected."));
    else if (done)
        return finishEnroll(tr("The fingerprint could not be added. Try again."));
    emit changed();
}

void FingerprintService::finishEnroll(const QString &error)
{
    const auto finger = m_enrolling;
    // Nothing more from this enrolment counts.
    m_bus.disconnect(service, m_device, deviceInterface, "EnrollStatus", this, SLOT(enrollStatus(QString, bool)));
    m_error = error;
    m_message = error.isEmpty() ? tr("Your %1 is added.").arg(fingerTitle(finger).toLower()) : QString();
    // A finished enrolment must be stopped before the reader is given back.
    call("EnrollStop", {},
         [this, finger, error](const QDBusMessage &)
         {
             release();
             m_enrolling.clear();
             refresh();
             if (error.isEmpty())
                 emit enrolled(finger);
         });
}

void FingerprintService::cancel()
{
    if (m_enrolling.isEmpty())
        return;
    const bool started = m_claimed;
    m_enrolling.clear();
    m_message.clear();
    if (started)
        call("EnrollStop", {}, [this](const QDBusMessage &) { release(); });
    emit changed();
}

void FingerprintService::remove(const QString &finger)
{
    if (busy() || m_device.isEmpty() || !m_fingers.contains(finger))
        return;
    m_error.clear();
    m_message.clear();
    claimed(
        [this, finger]
        {
            call("DeleteEnrolledFinger", {finger},
                 [this, finger](const QDBusMessage &reply)
                 {
                     if (reply.type() == QDBusMessage::ReplyMessage)
                         m_message = tr("Your %1 is deleted.").arg(fingerTitle(finger).toLower());
                     else
                         failed(reply, tr("The fingerprint was not deleted"));
                     release();
                     refresh();
                 },
                 true);
        });
}

void FingerprintService::runHelper(const QStringList &arguments, bool elevated)
{
    auto &process = elevated ? m_helper : m_status;
    if (process.state() != QProcess::NotRunning)
        return;
    if (!QFileInfo(m_helperPath).isFile())
    {
        m_loginAvailable = false;
        return;
    }
    if (elevated && !m_elevate.isEmpty())
        process.start(m_elevate.first(), m_elevate.mid(1) + QStringList{m_helperPath} + arguments);
    else
        process.start("/usr/bin/python3", QStringList{"-I", m_helperPath} + arguments);
    emit changed();
}

void FingerprintService::setLogin(bool login, bool sudo)
{
    if (changing())
        return;
    m_error.clear();
    m_message.clear();
    runHelper({"set", login ? "on" : "off", sudo ? "on" : "off"}, true);
}

void FingerprintService::installSupport()
{
    if (changing())
        return;
    m_error.clear();
    m_message = tr("Installing fingerprint support…");
    runHelper({"install-support"}, true);
}
