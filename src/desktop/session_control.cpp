#include "session_control.hpp"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <sys/stat.h>
#include <utility>
#include <unistd.h>

namespace
{
QString controlPath()
{
    return qEnvironmentVariable("PLATINUM_SESSION_CONTROL");
}
} // namespace

SessionControl::SessionControl(QObject *parent) : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(5000);
    connect(m_timeout, &QTimer::timeout, this, [this] { finish(false, {}); });
}

bool SessionControl::available() const
{
    const auto path = controlPath();
    if (path.isEmpty() || !QFileInfo(path).isAbsolute())
        return false;
    struct stat info {};
    return lstat(path.toLocal8Bit().constData(), &info) == 0 && S_ISSOCK(info.st_mode) &&
           info.st_uid == getuid();
}

bool SessionControl::busy() const
{
    return m_socket != nullptr;
}

bool SessionControl::request(const QByteArray &command)
{
    if (busy() || !available() || !command.endsWith('\n') || command.size() > 256)
        return false;
    m_pending = command;
    m_reply.clear();
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, [this] { m_socket->write(m_pending); });
    connect(m_socket, &QLocalSocket::readyRead, this, [this]
            {
                m_reply += m_socket->readAll();
                if (m_reply.size() > 64 * 1024)
                    finish(false, {});
            });
    // The supervisor answers with one JSON object and closes the connection.
    connect(m_socket, &QLocalSocket::disconnected, this, [this]
            {
                if (!m_socket)
                    return;
                m_reply += m_socket->readAll();
                const auto state = QJsonDocument::fromJson(m_reply).object().toVariantMap();
                finish(state.value("ok").toBool(), state);
            });
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error)
            {
                if (error != QLocalSocket::PeerClosedError)
                    finish(false, {});
            });
    m_timeout->start();
    emit busyChanged();
    m_socket->connectToServer(controlPath());
    return true;
}

void SessionControl::finish(bool ok, const QVariantMap &state)
{
    if (!m_socket)
        return;
    m_timeout->stop();
    auto *socket = std::exchange(m_socket, nullptr);
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
    emit busyChanged();
    emit finished(ok, state);
}
