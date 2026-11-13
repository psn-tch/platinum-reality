#pragma once
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Sound ▸ Alert Sounds ▸ "Mac sounds only" (docs/alert-sounds.md; Lily Hewitt, 2026-11-12: the Arch
// and KDE desktop sounds "GONE", as an option for everyone to remove them or download them again).
// The root helper platinum-system-sounds makes the change through pkexec (an administrator's
// password); reading the state needs none. The helper and pkexec are injectable for tests.
class SystemSounds final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // pacman keeps the desktop sounds out (our NoExtract line).
    Q_PROPERTY(bool removed READ removed NOTIFY changed)
    // Desktop sound folders still on disk.
    Q_PROPERTY(QStringList present READ present NOTIFY changed)
    Q_PROPERTY(bool changing READ changing NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    SystemSounds(QString helper, QStringList elevate = {"pkexec"}, QObject *parent = nullptr);
    ~SystemSounds() override;
    bool available() const;
    bool removed() const { return m_removed; }
    QStringList present() const { return m_present; }
    bool changing() const { return m_change.state() != QProcess::NotRunning; }
    QString error() const { return m_error; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setRemoved(bool removed);

  signals:
    void changed();

  private:
    void finished(QProcess *process, int code, QProcess::ExitStatus status);
    QString m_helper;
    QStringList m_elevate;
    QProcess m_status, m_change;
    QStringList m_present;
    QString m_error;
    bool m_removed = false;
};
