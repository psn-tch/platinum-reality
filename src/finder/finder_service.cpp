#include "finder_service.hpp"
#include <algorithm>
#include "window_surface_id.hpp"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QMessageBox>

FinderService::FinderService(QString root, QString profile, QObject *parent)
    : QObject(parent), m_root(std::move(root)), m_profile(std::move(profile)),
      m_operations(m_root, FinderPlaces::trashFor(m_profile), FinderPlaces::volumes)
{
}
int FinderService::windowCount() const
{
    int count = 0;
    for (const auto &window : m_windows)
        if (window)
            ++count;
    return count;
}
void FinderService::queryPower()
{
    if (m_powerQueried || qEnvironmentVariable("FRANKENSTEIN_ISOLATED") != "1")
        return;
    m_powerQueried = true;
    auto message = QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                  "org.frankenstein.Desktop", "PowerAvailable");
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]
    {
        const QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        m_powerAvailable = !reply.isError() && reply.value();
        for (const auto &window : m_windows)
            if (window)
                window->setPowerAvailable(*m_powerAvailable);
    });
}
bool FinderService::desktopPeer() const
{
    if (!calledFromDBus() || !connection().interface())
        return false;
    const QDBusReply<QString> owner =
        connection().interface()->serviceOwner("org.frankenstein.Desktop");
    const QDBusReply<uint> pid = connection().interface()->servicePid(message().service());
    if (!owner.isValid() || owner.value() != message().service() || !pid.isValid() || !pid.value())
        return false;
    const auto actual = QFileInfo(QString("/proc/%1/exe").arg(pid.value())).symLinkTarget();
    const auto desktop =
        QFileInfo(QCoreApplication::applicationDirPath() + "/frankenstein").canonicalFilePath();
    const auto tests =
        QFileInfo(QCoreApplication::applicationDirPath() + "/native-tests").canonicalFilePath();
    const bool testVenue = qEnvironmentVariable("FRANKENSTEIN_NATIVE_TEST") == "1" &&
                           !tests.isEmpty() && actual == tests;
    if ((desktop.isEmpty() || actual != desktop) && !testVenue)
        return false;
    return true;
}
bool FinderService::FinishFileDrag(QString token)
{
    return desktopPeer() && FinderWindow::finishFileDrag(token);
}
QString FinderService::DropDirectory(uint surfaceId, int x, int y)
{
    if (!surfaceId || !desktopPeer())
        return {};
    for (const auto &window : m_windows)
        if (window && finderSurfaceId(window->windowHandle()) == surfaceId)
            return window->dropDirectoryAt({x, y});
    return {};
}
QVariantMap FinderService::DirectoryState(QString path)
{
    if (!m_operations.readableDirectory(path))
        return {};
    const auto window = m_windows.value(QFileInfo(path).canonicalFilePath());
    return window ? window->directoryState() : QVariantMap{};
}
bool FinderService::OpenDirectory(QString path)
{
    if (!m_operations.readableDirectory(path))
        return false;
    const auto key = QFileInfo(path).canonicalFilePath();
    if (auto window = m_windows.value(key))
    {
        window->show();
        window->raise();
        window->activateWindow();
        return true;
    }
    auto *window = new FinderWindow(m_root, m_profile, key);
    m_windows[key] = window;
    connect(window, &FinderWindow::openFolder, this, &FinderService::OpenDirectory);
    connect(window, &FinderWindow::windowMenuRequested, this,
            [this, safe = QPointer(window)](QMenu *menu)
            {
                QList<QPair<QString, QString>> windows; // title, key
                for (auto it = m_windows.cbegin(); it != m_windows.cend(); ++it)
                    if (it.value())
                        windows.push_back({it.value()->windowTitle(), it.key()});
                std::sort(windows.begin(), windows.end(), [](const auto &a, const auto &b)
                          { return QString::localeAwareCompare(a.first, b.first) < 0; });
                for (const auto &[title, key] : windows)
                {
                    auto *item = menu->addAction(title);
                    item->setObjectName("window:" + key);
                    item->setCheckable(true);
                    item->setChecked(safe && m_windows.value(key) == safe);
                    connect(item, &QAction::triggered, this, [this, key] { OpenDirectory(key); });
                }
            });
    window->setPowerAvailable(m_powerAvailable.value_or(false));
    queryPower();
    connect(window, &FinderWindow::sessionRequested, this,
            [this, safe = QPointer(window)](const QString &command)
            {
                auto message = QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                              "org.frankenstein.Desktop", "EndSession");
                message << command;
                auto *watcher = new QDBusPendingCallWatcher(
                    QDBusConnection::sessionBus().asyncCall(message, 5000), this);
                connect(watcher, &QDBusPendingCallWatcher::finished, this,
                        [watcher, safe]
                        {
                            const QDBusPendingReply<bool> reply = *watcher;
                            watcher->deleteLater();
                            if ((reply.isError() || !reply.value()) && safe)
                                QMessageBox::warning(safe, "Special", "The desktop could not do this now.");
                        });
            });
    connect(window, &FinderWindow::openDocument, this,
            [this, safe = QPointer(window)](const QString &path, bool chooseApplication)
            {
                if (qEnvironmentVariable("FRANKENSTEIN_ISOLATED") != "1")
                {
                    QMessageBox::information(
                        safe, "Open Document",
                        "Open documents inside the Platinum Reality desktop session.");
                    return;
                }
                auto message =
                    QDBusMessage::createMethodCall("org.frankenstein.Desktop", "/Desktop",
                                                   "org.frankenstein.Desktop", "OpenDocument");
                message << path << chooseApplication;
                auto *watcher = new QDBusPendingCallWatcher(
                    QDBusConnection::sessionBus().asyncCall(message, 5000), this);
                connect(watcher, &QDBusPendingCallWatcher::finished, this,
                        [watcher, safe]
                        {
                            const QDBusPendingReply<bool> reply = *watcher;
                            watcher->deleteLater();
                            if ((reply.isError() || !reply.value()) && safe)
                                QMessageBox::warning(safe, "Open Document",
                                                     "The desktop could not open this document.");
                        });
            });
    connect(window, &QObject::destroyed, this,
            [this, key]
            {
                if (!m_windows.value(key))
                    m_windows.remove(key);
            });
    window->show();
    return true;
}
