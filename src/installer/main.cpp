#include "appearance/interface_font.hpp"
#include "engine_client.hpp"
#include "finder/platinum_style.hpp"
#include "installer_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QProcess>

namespace
{
QString readText(const QString &path, const QString &fallback)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.size() < (1 << 20) ? QString::fromUtf8(file.readAll()) : fallback;
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Install Platinum Reality");
    app.setDesktopFileName("org.platinumreality.Installer");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    QCommandLineParser parser;
    parser.setApplicationDescription("Installs Platinum Reality on a whole disk, using the install engine.");
    parser.addHelpOption();
    QCommandLineOption engine("engine", "The install engine program.", "program",
                              qEnvironmentVariable("PLATINUM_INSTALLER_ENGINE", "/usr/bin/platinum-reality-install"));
    QCommandLineOption share("share", "Folder with ReadMe.txt and LICENSE.", "folder",
                             "/usr/share/platinum-reality/installer");
    parser.addOptions({engine, share});
    parser.process(app);
    const auto folder = parser.value(share);
    EngineClient client(parser.value(engine));
    InstallerWindow window(&client,
                           readText(folder + "/ReadMe.txt",
                                    "Platinum Reality is a Mac OS 9-style desktop for Arch Linux.\n\n"
                                    "The installer erases the whole disk you choose. Back up anything you need first."),
                           readText(folder + "/LICENSE", "MIT License"));
    QObject::connect(&window, &InstallerWindow::restartRequested, &app,
                     [] { QProcess::startDetached("systemctl", {"reboot"}); });
    window.show();
    return app.exec();
}
