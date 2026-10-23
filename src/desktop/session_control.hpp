#pragma once
#include <QByteArray>
#include <QObject>
#include <QVariantMap>

class QLocalSocket;
class QTimer;

// Requests to the private session supervisor (scripts/standalone-session.py) over its owned
// control socket, named by PLATINUM_SESSION_CONTROL. One request at a time; the reply is the
// supervisor's JSON state with an "ok" field. The desktop never talks to any other compositor
// or to the user's own Sway socket.
class SessionControl final : public QObject
{
    Q_OBJECT
  public:
    explicit SessionControl(QObject *parent = nullptr);
    // The variable is set and names a socket owned by this user.
    bool available() const;
    bool busy() const;
    // False when unavailable or already busy. The command must end with a newline.
    bool request(const QByteArray &command);

  signals:
    void finished(bool ok, QVariantMap state);
    void busyChanged();

  private:
    void finish(bool ok, const QVariantMap &state);
    QLocalSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QByteArray m_pending, m_reply;
};
