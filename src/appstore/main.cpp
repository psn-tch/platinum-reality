#include "app_store_window.hpp"
#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "package_client.hpp"
#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("App Store");
    app.setDesktopFileName("org.platinumreality.AppStore");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    QCommandLineParser parser;
    parser.setApplicationDescription("Finds, installs and removes software (docs/app-store.md).");
    parser.addHelpOption();
    QCommandLineOption helper("helper", "The App Store's helper program.", "program",
                              qEnvironmentVariable("PLATINUM_PACKAGES_HELPER",
                                                   "/usr/lib/platinum-reality/platinum-packages"));
    parser.addOption(helper);
    parser.process(app);
    PackageClient client(parser.value(helper));
    AppStoreWindow window(&client);
    window.show();
    return app.exec();
}
