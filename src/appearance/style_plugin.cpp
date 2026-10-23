#include "finder/platinum_style.hpp"
#include <QStylePlugin>
#include <QGuiApplication>

// A build-local plugin. Child environments opt in; no host toolkit settings or
// installed plugin directory is changed.
class FrankensteinStylePlugin final : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QStyleFactoryInterface" FILE "style.json")
  public:
    QStyle *create(const QString &key) override
    {
        if (key.compare("Frankenstein", Qt::CaseInsensitive) != 0)
            return nullptr;
        // The bridge currently carries X window identities, not XID dbusmenu
        // endpoints. Qt would otherwise see our registrar and hide its local
        // menu with no usable replacement. Keep X11 menus inside their windows;
        // Wayland applications retain the existing native global-menu path.
        if (QGuiApplication::platformName() == "xcb")
            QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
        return new PlatinumStyle;
    }
};
#include "style_plugin.moc"
