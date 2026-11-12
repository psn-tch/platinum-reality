#pragma once
#include "catalog.hpp"
#include <QObject>
#include <QProcess>

// Runs the App Store's helper, platinum-packages (docs/app-store.md): one run reads the catalog,
// another makes a change (install, remove, update, yay, the AUR list) and reports its progress.
class PackageClient final : public QObject
{
    Q_OBJECT
  public:
    explicit PackageClient(QString helper, QObject *parent = nullptr);
    ~PackageClient() override;
    // Reads the catalog; asked again while reading, it reads once more afterwards (a change finished).
    void loadCatalog();
    // Starts a change; false if one is already running.
    bool run(const QStringList &arguments);
    bool busy() const { return m_change.state() != QProcess::NotRunning; }
    bool loading() const { return m_catalog.state() != QProcess::NotRunning; }
    // The packages pacman installed, upgraded or removed during the last change.
    QStringList touched() const { return m_touched; }

  signals:
    void catalogLoaded(const Catalog &catalog);
    void catalogFailed(const QString &error);
    // step and steps are -1 when the line is not one of pacman's numbered steps.
    void progress(const QString &text, int step, int steps);
    // Every line of a change as the programs wrote it (pacman, yay, makepkg), for Activity.
    void output(const QString &line);
    // ok, or the reason it failed; cancelled when the password box was dismissed.
    void finished(bool ok, const QString &error, bool cancelled);

  private:
    void readChange();
    QString m_helper;
    QProcess m_catalog, m_change;
    QByteArray m_pending;
    QString m_error;
    bool m_cancelled = false, m_reload = false;
    QStringList m_touched;
};
