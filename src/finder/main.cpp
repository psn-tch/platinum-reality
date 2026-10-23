#include "finder_service.hpp"
#include "finder_window.hpp"
#include "platinum_style.hpp"
#include "appearance/interface_font.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QPointer>
#include <QTimer>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setDesktopFileName("org.frankenstein.Finder");
    app.setApplicationName("Finder");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {"root", "Workspace boundary", "path", QDir::currentPath() + "/var/workspace"});
    parser.addOption(
        {"profile", "Separate state directory", "path", QDir::currentPath() + "/var/profile"});
    parser.addOption({"open", "Folder to open", "path"});
    parser.addOption({"trash", "Trash directory (default: the profile's own)", "path"});
    parser.addOption({"volume", "A further folder the user may work in (mounted drives)", "path"});
    parser.addOption({"exit-after", "Exit after milliseconds", "ms"});
    parser.process(app);
    QDir().mkpath(parser.value("root"));
    auto root = QFileInfo(parser.value("root")).canonicalFilePath();
    auto profile = QFileInfo(parser.value("profile")).absoluteFilePath();
    if (parser.isSet("trash"))
        FinderPlaces::trash = QDir::cleanPath(QFileInfo(parser.value("trash")).absoluteFilePath());
    FinderPlaces::volumes = parser.values("volume");
    FinderService finder(root, profile);
    const auto directory = parser.isSet("open") ? parser.value("open") : root;
    if (qEnvironmentVariable("FRANKENSTEIN_ISOLATED") == "1")
    {
        auto bus = QDBusConnection::sessionBus();
        const auto name =
            "org.frankenstein.Finder.p" +
            QString::fromLatin1(QCryptographicHash::hash((root + "\n" + profile).toUtf8(),
                                                         QCryptographicHash::Sha256)
                                    .toHex()
                                    .left(24));
        if (!bus.isConnected())
        {
            qCritical("Finder requires the private session bus.");
            return 2;
        }
        if (!bus.registerService(name))
        {
            QDBusInterface existing(name, "/Finder", "org.frankenstein.Finder", bus);
            QDBusReply<bool> reply = existing.call("OpenDirectory", directory);
            if (!reply.isValid() || !reply.value())
            {
                qCritical("The existing Finder could not open this directory.");
                return 2;
            }
            return 0;
        }
        if (!bus.registerObject("/Finder", &finder, QDBusConnection::ExportAllSlots))
        {
            qCritical("Finder service registration failed.");
            return 2;
        }
    }
    if (!finder.OpenDirectory(directory))
    {
        qCritical("Folder outside workspace or unavailable.");
        return 2;
    }
    if (parser.isSet("exit-after"))
        QTimer::singleShot(parser.value("exit-after").toInt(), &app, &QCoreApplication::quit);
    return app.exec();
}
