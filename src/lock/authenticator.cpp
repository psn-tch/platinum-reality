#include "authenticator.hpp"
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QRegularExpression>
#include <QThread>
#include <cstdlib>
#include <cstring>
#include <security/pam_appl.h>

namespace
{
struct Conversation
{
    QByteArray password;
    Authenticator *owner;
};

int converse(int count, const pam_message **messages, pam_response **responses, void *data)
{
    auto *conversation = static_cast<Conversation *>(data);
    if (count <= 0 || count > PAM_MAX_NUM_MSG)
        return PAM_CONV_ERR;
    auto *replies = static_cast<pam_response *>(calloc(size_t(count), sizeof(pam_response)));
    if (!replies)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i)
    {
        const auto style = messages[i]->msg_style;
        if (style == PAM_PROMPT_ECHO_OFF || style == PAM_PROMPT_ECHO_ON)
        {
            replies[i].resp = strdup(conversation->password.constData());
            if (!replies[i].resp)
            {
                for (int j = 0; j < i; ++j)
                    if (replies[j].resp)
                    {
                        explicit_bzero(replies[j].resp, strlen(replies[j].resp));
                        free(replies[j].resp);
                    }
                free(replies);
                return PAM_BUF_ERR;
            }
        }
        else if (style == PAM_ERROR_MSG || style == PAM_TEXT_INFO)
        {
            const auto text = QString::fromLocal8Bit(messages[i]->msg ? messages[i]->msg : "").left(200);
            const bool error = style == PAM_ERROR_MSG;
            auto *owner = conversation->owner;
            QMetaObject::invokeMethod(owner, [owner, text, error] { emit owner->message(text, error); },
                                      Qt::QueuedConnection);
        }
    }
    *responses = replies;
    return PAM_SUCCESS;
}
} // namespace

Authenticator::Authenticator(QString service, QString user, QString confdir, QObject *parent)
    : QObject(parent), m_service(std::move(service)), m_user(std::move(user)), m_confdir(std::move(confdir))
{
}

Authenticator::~Authenticator()
{
    // A PAM module may still be waiting (a fingerprint reader, a slow directory): never
    // destroy the conversation it is using. The process is exiting when this runs.
    if (m_worker)
        m_worker->wait();
}

bool Authenticator::submit(const QString &password)
{
    if (m_busy.exchange(true))
        return false;
    if (m_worker)
        m_worker->wait();
    delete m_worker;
    m_worker = QThread::create(
        [this, secret = password.toUtf8()]() mutable
        {
            Conversation conversation{std::move(secret), this};
            const pam_conv conv{converse, &conversation};
            pam_handle_t *handle = nullptr;
            const auto service = m_service.toLocal8Bit(), user = m_user.toLocal8Bit(),
                       confdir = m_confdir.toLocal8Bit();
            int result = m_confdir.isEmpty()
                             ? pam_start(service.constData(), user.constData(), &conv, &handle)
                             : pam_start_confdir(service.constData(), user.constData(), &conv,
                                                 confdir.constData(), &handle);
            if (result == PAM_SUCCESS)
                result = pam_authenticate(handle, 0);
            if (result == PAM_SUCCESS)
                pam_setcred(handle, PAM_REFRESH_CRED); // As swaylock; a failure here does not lock out.
            if (handle)
                pam_end(handle, result);
            explicit_bzero(conversation.password.data(), size_t(conversation.password.size()));
            const bool success = result == PAM_SUCCESS;
            QMetaObject::invokeMethod(this,
                                      [this, success]
                                      {
                                          m_busy = false;
                                          emit finished(success);
                                      },
                                      Qt::QueuedConnection);
        });
    m_worker->start();
    return true;
}

bool pamServiceUsesFingerprint(const QString &service, const QString &confdir)
{
    const auto root = confdir.isEmpty() ? QStringLiteral("/etc/pam.d") : confdir;
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9_.-]{1,64}$"));
    // Depth- and count-bounded walk of the auth lines; services are plain names in root.
    QStringList pending{service}, seen;
    while (!pending.isEmpty() && seen.size() < 32)
    {
        const auto name = pending.takeFirst();
        if (!safeName.match(name).hasMatch() || seen.contains(name))
            continue;
        seen.append(name);
        QFile file(QDir(root).filePath(name));
        if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
            continue;
        for (auto line : QString::fromUtf8(file.readAll()).split('\n'))
        {
            line = line.section('#', 0, 0).simplified();
            if (line.startsWith("@include "))
            {
                pending.append(line.section(' ', 1, 1));
                continue;
            }
            const auto fields = line.split(' ', Qt::SkipEmptyParts);
            // "-auth" is auth whose module may be absent.
            if (fields.size() < 3 || (fields[0] != "auth" && fields[0] != "-auth"))
                continue;
            if (fields[1] == "include" || fields[1] == "substack")
                pending.append(fields[2]);
            else
            {
                // The module follows the control field, which may be a [bracketed list].
                const auto module = line.contains(']') ? line.section(']', 1).simplified().section(' ', 0, 0)
                                                       : fields[2];
                if (module.endsWith("pam_fprintd.so"))
                    return true;
            }
        }
    }
    return false;
}
