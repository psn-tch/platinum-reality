#include "editor_window.hpp"
#include "finder/platinum_style.hpp"
#include "appearance/interface_font.hpp"
#include <QApplication>
#include <QCommandLineParser>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Editor");
    app.setDesktopFileName("org.frankenstein.Editor");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument("file", "Text document");
    parser.process(app);
    EditorWindow window(
        parser.positionalArguments().isEmpty() ? QString() : parser.positionalArguments().first());
    window.show();
    return app.exec();
}
