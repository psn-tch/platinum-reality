#include "appearance/interface_font.hpp"
#include "finder/platinum_style.hpp"
#include "puzzle_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QRandomGenerator>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("Puzzle");
    app.setDesktopFileName("org.frankenstein.Puzzle");
    app.setStyle(new PlatinumStyle);
    app.setFont(classicInterfaceFont());
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.process(app);
    SlidingPuzzle puzzle;
    puzzle.shuffle(QRandomGenerator::global()->generate());
    PuzzleWindow window(std::move(puzzle));
    window.show();
    return app.exec();
}
