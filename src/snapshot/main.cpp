#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "snapshot_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QStandardPaths>
#include <unistd.h>

namespace
{
// Older webcams (gspca: Lily Hewitt's PS2 EyeToy) send formats such as raw JPEG frames or Bayer that
// Qt's camera support may not take. libv4l's v4l2convert offers every camera's picture in plain
// formats too, so Snapshot runs with it when it is installed (v4l-utils), once, before anything else.
void withLibv4l(char **argv)
{
    const char *convert = "/usr/lib/libv4l/v4l2convert.so";
    if (qEnvironmentVariableIsSet("PLATINUM_SNAPSHOT_NO_LIBV4L") || access(convert, R_OK) != 0)
        return;
    const auto preload = qgetenv("LD_PRELOAD");
    if (preload.contains("v4l2convert.so"))
        return;
    qputenv("LD_PRELOAD", preload.isEmpty() ? QByteArray(convert) : preload + ":" + convert);
    execv("/proc/self/exe", argv);
    // execv returned: carry on without it.
    qputenv("LD_PRELOAD", preload);
}
} // namespace

int main(int argc, char **argv)
{
    withLibv4l(argv);
    QApplication app(argc, argv);
    app.setApplicationName("Snapshot");
    app.setDesktopFileName("org.platinumreality.Snapshot");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    QCommandLineParser parser;
    parser.setApplicationDescription("Takes pictures with the camera (docs/snapshot.md).");
    parser.addHelpOption();
    parser.process(app);
    auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QDir::home().filePath("Pictures");
    SnapshotWindow window(QDir(pictures + "/Snapshot"));
    window.show();
    return app.exec();
}
