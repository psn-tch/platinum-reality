#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QFile output(qEnvironmentVariable("FRANKENSTEIN_PROBE_RESULT"));
    if (!output.open(QIODevice::WriteOnly))
        return 2;
    QJsonObject environment;
    for (const auto &key :
         {"WAYLAND_DISPLAY", "DISPLAY", "XAUTHORITY", "WAYLAND_SOCKET", "QT_STYLE_OVERRIDE",
          "QT_PLUGIN_PATH", "XDG_CONFIG_HOME", "XDG_CURRENT_DESKTOP", "DESKTOP_STARTUP_ID",
          "XDG_ACTIVATION_TOKEN", "GSETTINGS_BACKEND"})
        environment[key] = qEnvironmentVariable(key);
    return output.write(QJsonDocument(QJsonObject{{"arguments", QJsonArray::fromStringList(
                                                                    app.arguments().mid(1))},
                                                  {"directory", QDir::currentPath()},
                                                  {"environment", environment}})
                            .toJson()) > 0
               ? 0
               : 3;
}
