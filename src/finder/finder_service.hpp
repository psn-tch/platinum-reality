#pragma once
#include "finder_window.hpp"
#include <QDBusContext>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <optional>

// One Finder owns all folder windows within a profile. The native launcher uses
// a private session bus; standalone/offscreen use need not register a service.
class FinderService final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.frankenstein.Finder")
  public:
    FinderService(QString root, QString profile, QObject *parent = nullptr);
    int windowCount() const;
  public slots:
    bool OpenDirectory(QString path);
    QVariantMap DirectoryState(QString path);
    bool FinishFileDrag(QString token);
    QString DropDirectory(uint surfaceId, int x, int y);

  private:
    bool desktopPeer() const;
    // Asks the desktop once whether it offers Sleep, Restart and Shut Down; every window follows.
    void queryPower();
    QString m_root, m_profile;
    std::optional<bool> m_powerAvailable;
    bool m_powerQueried = false;
    FileOperations m_operations;
    QMap<QString, QPointer<FinderWindow>> m_windows;
};
