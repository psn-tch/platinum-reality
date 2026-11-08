#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>

// Platinum Asset Importer (docs/asset-importer.md): runs scripts/import-macos9-assets.py, which
// reads the user's own Mac OS 9 disk, CD or System Folder and writes their sounds, icons, cursors
// and pictures to their private assets folder. Nothing is downloaded; the source is never written.
class AssetImporter final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool finding READ finding NOTIFY busyChanged)
    // Likely sources: {path, name, kind ("image" or "folder"), size}.
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    // The last import's report, one entry per kind of asset: {name, imported, kept, skipped}.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY finished)
    Q_PROPERTY(QString error READ error NOTIFY finished)
    Q_PROPERTY(QString assets READ assets CONSTANT)
    // Where to look for sources; empty means the home folder and mounted disks. Tests set a folder.
    Q_PROPERTY(QStringList searchRoots MEMBER m_roots)
  public:
    AssetImporter(QString helper, QString assets, QObject *parent = nullptr);
    ~AssetImporter() override;
    bool busy() const { return m_busy; }
    bool finding() const { return m_busy && m_finding; }
    QVariantList sources() const { return m_sources; }
    QVariantList sections() const { return m_sections; }
    QString error() const { return m_error; }
    QString assets() const { return m_assets; }
    Q_INVOKABLE void findSources();
    Q_INVOKABLE bool importFrom(const QString &path, bool replace);
  signals:
    void busyChanged();
    void sourcesChanged();
    void finished();
  private:
    void run(const QStringList &arguments, bool finding);
    QString m_helper, m_assets, m_error;
    QVariantList m_sources, m_sections;
    QStringList m_roots;
    bool m_busy = false, m_finding = false, m_timedOut = false;
    QProcess m_process;
    QTimer m_timeout;
};
