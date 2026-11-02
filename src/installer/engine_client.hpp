#pragma once
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <functional>

// The installer's only way to touch disks: Rocky's install engine (distro/installer-engine),
// run as a separate program with the contract recorded in HANDOFF.md:
//   disks --json                       -> [{path, model, size_bytes, removable, transport, is_boot_medium, partitions}]
//   plan --config FILE --json          -> {ok, erases, steps, warnings}; changes nothing
//   install --config FILE --confirm-erase DISK --progress-fd 1
//                                      -> JSON lines {step, fraction, message}, then {result, message}
//   choices --json                     -> {locales: [..], timezones: [..], keymaps: [..]}: what this live
//                                         image can install (additive; older engines lack it)
// Passwords never go in arguments or files: install reads one JSON object from standard input.
class EngineClient final : public QObject
{
    Q_OBJECT
  public:
    explicit EngineClient(QString program, QObject *parent = nullptr);
    QString program() const { return m_program; }
    // A command the window waits for; the background choices query does not count.
    bool busy() const { return running() && !m_background; }
    bool running() const { return m_process && m_process->state() != QProcess::NotRunning; }
    void listDisks();
    // Emits choicesListed, with an empty object when the engine cannot say (the window keeps its own lists).
    void listChoices();
    void plan(const QJsonObject &config);
    // secrets: {"password": ..., "passphrase": ...}; written to the engine's standard input only.
    void install(const QJsonObject &config, const QJsonObject &secrets, const QString &confirmDisk);
    void cancel();

  signals:
    void disksListed(QVariantList disks);
    void choicesListed(QJsonObject choices);
    void planned(QJsonObject plan);
    void progress(QString step, double fraction, QString message);
    void installFinished(bool ok, QString message);
    void failed(QString message);

  private:
    QProcess *start(const QStringList &arguments);
    // While another command runs, do this once it has finished; true when deferred.
    bool afterCurrent(std::function<void()> again);
    QString writeConfig(const QJsonObject &config);
    void readProgress();
    QString m_program, m_configPath, m_lastStep;
    QProcess *m_process = nullptr;
    QByteArray m_pending;
    bool m_finished = false, m_background = false;
};
