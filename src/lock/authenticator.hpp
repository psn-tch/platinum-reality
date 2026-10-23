#pragma once
#include <QObject>
#include <QString>
#include <atomic>

// Password authentication through PAM, one transaction per attempt, off the UI thread.
// Prompts are answered with the typed password, as swaylock does; PAM's own information and
// error messages are passed on for display. The caller never sees the decision's inputs.
class Authenticator final : public QObject
{
    Q_OBJECT
  public:
    // confdir is for qualification only (pam_start_confdir); empty uses /etc/pam.d.
    Authenticator(QString service, QString user, QString confdir = {}, QObject *parent = nullptr);
    ~Authenticator() override;
    // Returns false while an attempt is already running.
    bool submit(const QString &password);
    bool busy() const { return m_busy; }

  signals:
    void message(const QString &text, bool error);
    void finished(bool success);

  private:
    QString m_service, m_user, m_confdir;
    std::atomic_bool m_busy = false;
    class QThread *m_worker = nullptr;
};

// Whether a PAM service's auth stack (following include/substack/@include) uses pam_fprintd,
// i.e. whether the system allows fingerprint login at all. Bounded and read-only.
bool pamServiceUsesFingerprint(const QString &service, const QString &confdir = {});
