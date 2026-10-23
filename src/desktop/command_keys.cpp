#include "command_keys.hpp"
#include <QKeyEvent>
#include <QSet>
#include <QWindow>

namespace
{
constexpr uint escape = 1, home = 102, up = 103, left = 105, right = 106, end = 107, down = 108;

bool letter(uint key)
{
    return (key >= 16 && key <= 25) || (key >= 30 && key <= 38) || (key >= 44 && key <= 50);
}
bool digitOrPunctuation(uint key)
{
    // 1-0, minus, equal; brackets; semicolon, apostrophe, grave; backslash;
    // comma, period, slash.
    return (key >= 2 && key <= 13) || key == 26 || key == 27 || (key >= 39 && key <= 41) ||
           key == 43 || (key >= 51 && key <= 53);
}
} // namespace

std::optional<CommandChord> translateCommandKey(uint key, Qt::KeyboardModifiers modifiers,
                                                bool terminal)
{
    if (!modifiers.testFlag(Qt::MetaModifier) ||
        (modifiers & (Qt::ControlModifier | Qt::AltModifier)))
        return std::nullopt;
    const bool shift = modifiers.testFlag(Qt::ShiftModifier);
    if (key == 52 && !shift)
        return CommandChord{escape, false, false};
    if (key == left || key == right)
        return CommandChord{key == left ? home : end, false, shift};
    if (key == up || key == down)
        return CommandChord{key == up ? home : end, true, shift};
    if (letter(key))
        return CommandChord{key, true, shift || terminal};
    if (digitOrPunctuation(key))
        return CommandChord{key, true, shift};
    return std::nullopt;
}

bool isTerminalApplication(const QString &appId)
{
    static const QSet<QString> terminals{
        "org.kde.konsole", "konsole", "org.kde.yakuake", "yakuake", "foot", "footclient",
        "kitty", "alacritty", "com.mitchellh.ghostty", "ghostty", "org.wezfurlong.wezterm",
        "wezterm", "xterm", "uxterm", "urxvt", "rxvt", "st", "st-256color",
        "org.gnome.console", "kgx", "org.gnome.terminal", "gnome-terminal-server",
        "gnome-terminal", "org.gnome.ptyxis", "ptyxis", "terminator", "com.gexperts.tilix",
        "tilix", "xfce4-terminal", "org.xfce.terminal", "lxterminal", "qterminal",
        "terminology", "contour", "rio", "cool-retro-term", "deepin-terminal", "sakura",
        "guake", "tilda", "warp", "dev.warp.warp", "io.elementary.terminal"};
    auto id = appId.trimmed().toLower();
    if (id.endsWith(".desktop"))
        id.chop(8);
    return terminals.contains(id);
}

bool ShellTextCommands::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
    const bool shift = modifiers == (Qt::MetaModifier | Qt::ShiftModifier);
    if (modifiers != Qt::MetaModifier && !shift)
        return false;
    const char *command = nullptr;
    switch (key->key())
    {
    case Qt::Key_X: command = shift ? nullptr : "cut"; break;
    case Qt::Key_C: command = shift ? nullptr : "copy"; break;
    case Qt::Key_V: command = shift ? nullptr : "paste"; break;
    case Qt::Key_A: command = shift ? nullptr : "selectAll"; break;
    case Qt::Key_Z: command = shift ? "redo" : "undo"; break;
    default: break;
    }
    auto *window = qobject_cast<QWindow *>(watched);
    auto *focus = window ? window->focusObject() : nullptr;
    // Text inputs and editors (QML TextInput/TextEdit and their controls).
    if (!command || !focus || focus->metaObject()->indexOfMethod("paste()") < 0)
        return false;
    // Claim the chord before any shortcut sees it, then run it on the press.
    event->accept();
    if (event->type() == QEvent::KeyPress)
        QMetaObject::invokeMethod(focus, command);
    return true;
}
