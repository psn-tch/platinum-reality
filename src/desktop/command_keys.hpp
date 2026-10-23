#pragma once
#include <QObject>
#include <QString>
#include <Qt>
#include <optional>

// Command is the Super key (psn_tch's choice, Finishing School D06). Linux
// applications expect Control for the same commands, so a Command chord is
// delivered to the application as the key it understands:
//   letters, digits, punctuation  -> Control (Command-C copies, Command-V pastes)
//   in terminals, letters         -> Control-Shift (Control-C still interrupts)
//   Command-period                -> Escape (cancel, as on Mac OS)
//   Command-Left/Right            -> Home/End; Command-Up/Down -> document start/end
// Shift is kept. Chords that also hold Control or Option are not translated.
// Codes are Linux evdev key codes (physical keys), not characters.
struct CommandChord
{
    uint key = 0;
    bool control = false;
    bool shift = false;
};

std::optional<CommandChord> translateCommandKey(uint evdevKey, Qt::KeyboardModifiers modifiers,
                                                bool terminal);
// Terminal emulators by Wayland app_id or X11 class.
bool isTerminalApplication(const QString &appId);

// Command-X/C/V/Z (Shift-Z redoes) and Command-A in the shell's own text fields:
// search, names and dialogs, including inside modal panels where window
// shortcuts are blocked. Watches one window; application surfaces have no text
// commands, so their Command keys pass through untouched.
class ShellTextCommands final : public QObject
{
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
