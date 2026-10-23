#include "document_service.hpp"
#include "finder/file_operations.hpp"
#include "manager.hpp"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QFileInfo>
#include <QTimer>

DesktopDocumentService::DesktopDocumentService(NativeManager *manager)
    : QObject(manager), m_manager(manager)
{
    auto bus = QDBusConnection::sessionBus();
    m_registered = bus.registerService("org.frankenstein.Desktop");
    if (m_registered && !bus.registerObject("/Desktop", this, QDBusConnection::ExportAllSlots))
    {
        bus.unregisterService("org.frankenstein.Desktop");
        m_registered = false;
    }
    if (!m_registered)
        qWarning("The private Finder document bridge is unavailable.");
}
DesktopDocumentService::~DesktopDocumentService()
{
    if (!m_registered)
        return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject("/Desktop");
    bus.unregisterService("org.frankenstein.Desktop");
}
uint DesktopDocumentService::ownedPeerPid(const QString &binary) const
{
    if (!calledFromDBus() || !connection().interface())
        return 0;
    const QDBusReply<uint> pid = connection().interface()->servicePid(message().service());
    if (!pid.isValid() || !pid.value())
        return 0;
    // app_id and well-known bus names are self-reported. Verify the live bus
    // peer's executable against the owned Finder binary instead.
    const auto expected =
        QFileInfo(QCoreApplication::applicationDirPath() + '/' + binary).canonicalFilePath();
    const auto actual = QFileInfo(QString("/proc/%1/exe").arg(pid.value())).symLinkTarget();
    return !expected.isEmpty() && actual == expected ? pid.value() : 0;
}
bool DesktopDocumentService::OpenDocument(QString path, bool chooseApplication)
{
    if (!ownedPeerPid("frankenstein-finder"))
        return false;
    if (!m_manager->readableDocument(path))
        return false;
    m_manager->requestOpenDocument(path, chooseApplication);
    return true;
}
bool DesktopDocumentService::EndSession(QString command)
{
    static const QStringList commands{"log-out", "sleep", "restart", "shut-down"};
    if (!ownedPeerPid("frankenstein-finder") || !commands.contains(command))
        return false;
    if (command != "log-out" && !m_manager->powerAvailable())
        return false;
    // After this reply: ending the session closes the Finder's own windows.
    QTimer::singleShot(0, m_manager, [manager = m_manager, command]
    {
        if (command == "log-out")
            manager->quitSession();
        else if (command == "sleep")
            manager->sleepComputer();
        else if (command == "restart")
            manager->restartComputer();
        else
            manager->shutDownComputer();
    });
    return true;
}
bool DesktopDocumentService::PowerAvailable()
{
    return ownedPeerPid("frankenstein-finder") && m_manager->powerAvailable();
}
bool DesktopDocumentService::DropFiles(QStringList paths, QString token)
{
    const auto pid = ownedPeerPid("frankenstein-drop-receiver");
    return pid && m_manager->receiveDesktopDrop(pid, std::move(paths), std::move(token));
}
