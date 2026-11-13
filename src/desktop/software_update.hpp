#pragma once
#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// Platinum Software Update (docs/software-update.md): runs scripts/platinum-software-update, which
// checks GitHub Releases, downloads and verifies a release and installs it through pkexec. This
// side never installs anything itself. Automatic checks (off by default) only report.
class SoftwareUpdate final : public QObject
{
    Q_OBJECT
    // "", "status", "checking", "finding", "downloading", "verifying", "installing", "restoring"
    Q_PROPERTY(QString phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString installed READ installed NOTIFY changed)
    Q_PROPERTY(bool previousKept READ previousKept NOTIFY changed)
    // The newer release found by Check Now: {version, date, notes, size, tag}; empty when none.
    Q_PROPERTY(QVariantMap available READ available NOTIFY changed)
    // A verified release folder ready to install, and its description.
    Q_PROPERTY(QString ready READ ready NOTIFY changed)
    Q_PROPERTY(QVariantMap readyRelease READ readyRelease NOTIFY changed)
    // Release folders found on disks: {path, version}.
    Q_PROPERTY(QVariantList bundles READ bundles NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    // A new version was installed or the previous one restored: Restart Desktop puts it to use.
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
  public:
    SoftwareUpdate(QString client, QString keyring, QString settingsFile, QObject *parent = nullptr);
    ~SoftwareUpdate() override;
    QString phase() const { return m_phase; }
    bool busy() const { return !m_phase.isEmpty(); }
    QString installed() const { return m_installed; }
    bool previousKept() const { return m_previousKept; }
    QVariantMap available() const { return m_available; }
    QString ready() const { return m_ready; }
    QVariantMap readyRelease() const { return m_readyRelease; }
    QVariantList bundles() const { return m_bundles; }
    double progress() const { return m_progress; }
    QString error() const { return m_error; }
    QString message() const { return m_message; }
    bool checked() const { return m_checked; }
    bool automatic() const { return m_automatic; }
    void setAutomatic(bool on);
    QString lastChecked() const { return m_lastChecked.isValid() ? m_lastChecked.toString(Qt::ISODate) : QString(); }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void findBundles();
    Q_INVOKABLE void useLocal(const QString &folder);
    Q_INVOKABLE void install();
    Q_INVOKABLE void restorePrevious();
    // A newly installed computer: check now, and keep trying (across logins) until a check gets an
    // answer, since the network may not be up yet. Like an automatic check, it only reports.
    Q_INVOKABLE void checkAfterInstall();
    bool restartNeeded() const { return m_restartNeeded; }
  signals:
    void changed();
    // An automatic check found a newer release: the desktop may say so; nothing installs.
    void updateFound(const QString &version);
    // The installer finished: separate programs (the App Store) can reopen on the new version.
    void installFinished(const QString &version);
  private:
    void run(const QString &phase, const QStringList &arguments);
    void handle(const QJsonObject &message);
    void save() const;
    QString m_client, m_keyring, m_settings;
    QString m_phase, m_installed, m_ready, m_error, m_message;
    QVariantMap m_available, m_readyRelease;
    QVariantList m_bundles;
    double m_progress = 0;
    bool m_restartNeeded = false, m_previousKept = false, m_checked = false, m_automatic = false, m_silent = false, m_pending = false;
    QDateTime m_lastChecked;
    QByteArray m_buffer;
    QProcess m_process;
    QTimer m_schedule, m_retry;
};
