#pragma once
#include <QDBusContext>
#include <QObject>
class NativeManager;
// A narrow private-bus bridge for our own Finder; no arbitrary command API.
class DesktopDocumentService final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.frankenstein.Desktop")
  public:
    explicit DesktopDocumentService(NativeManager *);
    ~DesktopDocumentService() override;
  public slots:
    bool OpenDocument(QString path, bool chooseApplication);
    bool DropFiles(QStringList paths, QString token);
    // The Finder's Special menu: "log-out", "sleep", "restart" or "shut-down", performed as the
    // desktop's own Special menu does; and whether the last three are offered.
    bool EndSession(QString command);
    bool PowerAvailable();

  private:
    uint ownedPeerPid(const QString &binary) const;
    NativeManager *m_manager;
    bool m_registered = false;
};
