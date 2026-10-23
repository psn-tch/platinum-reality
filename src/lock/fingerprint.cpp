#include "fingerprint.hpp"
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QTimer>

namespace
{
const auto service = QStringLiteral("net.reactivated.Fprint");
const auto managerPath = QStringLiteral("/net/reactivated/Fprint/Manager");
const auto managerInterface = QStringLiteral("net.reactivated.Fprint.Manager");
const auto deviceInterface = QStringLiteral("net.reactivated.Fprint.Device");
} // namespace

Fingerprint::Fingerprint(QString user, QDBusConnection bus, QObject *parent)
    : QObject(parent), m_user(std::move(user)), m_bus(std::move(bus))
{
}

Fingerprint::~Fingerprint()
{
    stop();
}

void Fingerprint::call(const QString &method, const QVariantList &arguments,
                       std::function<void(const QDBusMessage &)> done, const QString &path)
{
    auto message = QDBusMessage::createMethodCall(service, path.isEmpty() ? m_device : path,
                                                  path.isEmpty() ? deviceInterface : managerInterface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 10000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [watcher, done = std::move(done)]
            {
                watcher->deleteLater();
                done(watcher->reply());
            });
}

void Fingerprint::start()
{
    if (!m_bus.isConnected())
        return unavailable("no system bus");
    call("GetDefaultDevice", {},
         [this](const QDBusMessage &reply)
         {
             if (m_stopped)
                 return;
             if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
                 return unavailable("no reader");
             m_device = qvariant_cast<QDBusObjectPath>(reply.arguments().first()).path();
             if (m_device.isEmpty())
                 return unavailable("no reader");
             call("ListEnrolledFingers", {m_user},
                  [this](const QDBusMessage &enrolled)
                  {
                      if (m_stopped)
                          return;
                      if (enrolled.type() != QDBusMessage::ReplyMessage || enrolled.arguments().isEmpty() ||
                          enrolled.arguments().first().toStringList().isEmpty())
                          return unavailable("no enrolled fingers");
                      call("Claim", {m_user},
                           [this](const QDBusMessage &claim)
                           {
                               if (claim.type() != QDBusMessage::ReplyMessage)
                                   return unavailable("reader busy or not permitted");
                               m_claimed = true;
                               if (m_stopped) // Unlocked meanwhile: give the reader back.
                                   return release();
                               // Only fprintd itself (the service owner) can report a match.
                               m_bus.connect(service, m_device, deviceInterface, "VerifyStatus", this,
                                             SLOT(verifyStatus(QString, bool)));
                               verify();
                           });
                  });
         },
         managerPath);
}

void Fingerprint::verify()
{
    call("VerifyStart", {QStringLiteral("any")},
         [this](const QDBusMessage &reply)
         {
             if (m_stopped)
                 return;
             if (reply.type() != QDBusMessage::ReplyMessage)
             {
                 if (++m_errors > 3)
                     return unavailable("verification failed to start");
                 QTimer::singleShot(1000, this, &Fingerprint::verify);
                 return;
             }
             if (!m_verifying)
             {
                 m_verifying = true;
                 emit availableChanged(true);
             }
         });
}

void Fingerprint::verifyStatus(const QString &result, bool done)
{
    if (m_stopped || !m_verifying)
        return;
    if (result == "verify-match")
    {
        m_verifying = false;
        emit matched();
        return;
    }
    if (result == "verify-disconnected")
        return unavailable("reader disconnected");
    if (result == "verify-no-match")
        emit status(tr("That fingerprint was not recognized. Try again."));
    else if (result == "verify-unknown-error")
        ++m_errors;
    else
        emit status(tr("Touch the fingerprint reader again."));
    if (!done)
        return;
    if (m_errors > 3)
        return unavailable("reader errors");
    // A finished scan must be stopped before the next one; pause so errors cannot spin.
    call("VerifyStop", {}, [this](const QDBusMessage &) {
        if (!m_stopped)
            QTimer::singleShot(300, this, &Fingerprint::verify);
    });
}

void Fingerprint::unavailable(const QString &reason)
{
    qInfo("Fingerprint unlock unavailable: %s", qPrintable(reason));
    const bool was = m_verifying;
    release();
    if (was)
        emit availableChanged(false);
}

void Fingerprint::release()
{
    if (m_claimed && !m_device.isEmpty())
    {
        if (m_verifying)
            m_bus.call(QDBusMessage::createMethodCall(service, m_device, deviceInterface, "VerifyStop"),
                       QDBus::Block, 2000);
        m_bus.call(QDBusMessage::createMethodCall(service, m_device, deviceInterface, "Release"),
                   QDBus::Block, 2000);
    }
    m_claimed = m_verifying = false;
}

void Fingerprint::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    release();
}
