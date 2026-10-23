#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantMap>

// Sound-panel service; querying starts only when the user opens the panel.
// All commands run asynchronously; only the most recent queued choice per control wins.
class AudioService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString warning READ warning NOTIFY errorChanged)
  public:
    explicit AudioService(QString helper, QObject *parent = nullptr, int timeout = 25000);
    ~AudioService() override;
    QVariantMap state() const;
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    QString warning() const { return m_warning; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void chooseOutput(const QString &name);
    Q_INVOKABLE void chooseInput(const QString &name);
    Q_INVOKABLE void chooseProfile(const QString &card, const QString &profile);
    Q_INVOKABLE void setVolume(const QString &kind, const QString &name, int value);
    Q_INVOKABLE void setMuted(const QString &kind, const QString &name, bool muted);
  signals:
    void stateChanged();
    void busyChanged();
    void errorChanged();
  private:
    struct Request { QString key; QStringList arguments; };
    void enqueue(Request request);
    void start(Request request);
    void next();
    void fail(const QString &message);
    void notifyState();
    void collectReply();
    void stopHelper();
    static bool validName(const QString &name);
    QString m_helper, m_error, m_warning;
    QVariantMap m_state;
    QList<Request> m_pending;
    QMap<QString, Request> m_requested;
    Request m_active;
    QProcess m_process;
    QTimer m_timeout;
    QByteArray m_reply;
    bool m_busy = false, m_refreshPending = false, m_timedOut = false;
    bool m_stateQueued = false;
    bool m_oversized = false;
};
