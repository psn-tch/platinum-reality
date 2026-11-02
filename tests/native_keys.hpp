#pragma once
#include <QWindow>
#include <qpa/qwindowsysteminterface.h>

// Test-only hardware-shaped input. The Qt Linux scan codes are evdev + 8.
inline void nativeKeyEvent(QWindow *window, QEvent::Type type, Qt::Key key,
                           Qt::KeyboardModifiers modifiers, uint evdevCode,
                           const QString &text = {})
{
    QWindowSystemInterface::handleExtendedKeyEvent(window, type, key, modifiers,
                                                  evdevCode + 8, 0, 0, text);
    QWindowSystemInterface::flushWindowSystemEvents();
}
inline void commandKey(QWindow *window, Qt::Key key, uint evdevCode)
{
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Meta, Qt::MetaModifier, 125);
    nativeKeyEvent(window, QEvent::KeyPress, key, Qt::MetaModifier, evdevCode);
    nativeKeyEvent(window, QEvent::KeyRelease, key, Qt::MetaModifier, evdevCode);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Meta, Qt::NoModifier, 125);
}
inline void altKey(QWindow *window, Qt::Key key, uint evdevCode)
{
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier, 56);
    nativeKeyEvent(window, QEvent::KeyPress, key, Qt::AltModifier, evdevCode);
    nativeKeyEvent(window, QEvent::KeyRelease, key, Qt::AltModifier, evdevCode);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier, 56);
}
// Control-Option-Escape: frees a pointer that a game locked or confined.
inline void pointerReleaseChord(QWindow *window)
{
    const auto both = Qt::ControlModifier | Qt::AltModifier;
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier, 29);
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Alt, both, 56);
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Escape, both, 1);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Escape, both, 1);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::ControlModifier, 56);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier, 29);
}
// Command-Shift-digit: Shift-3 and Shift-4 arrive as # and $ (US layout), as from a keyboard.
inline void commandShiftKey(QWindow *window, Qt::Key shifted, uint evdevCode)
{
    const auto both = Qt::MetaModifier | Qt::ShiftModifier;
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Meta, Qt::MetaModifier, 125);
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Shift, both, 42);
    nativeKeyEvent(window, QEvent::KeyPress, shifted, both, evdevCode);
    nativeKeyEvent(window, QEvent::KeyRelease, shifted, both, evdevCode);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Shift, Qt::MetaModifier, 42);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Meta, Qt::NoModifier, 125);
}
inline void screenshotKey(QWindow *window)
{
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier, 56);
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Shift, Qt::AltModifier | Qt::ShiftModifier, 42);
    nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_S, Qt::AltModifier | Qt::ShiftModifier, 31);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_S, Qt::AltModifier | Qt::ShiftModifier, 31);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Shift, Qt::AltModifier, 42);
    nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier, 56);
}

// Controlled US-letter/space/path fixtures only. QTest's char overload supplies an
// uppercase text string without a physical Shift press; a compositor's XKB
// translation can correctly turn that into lowercase. Supply the actual chord.
inline bool typeAscii(QWindow *window, const QByteArray &text)
{
    const QByteArray letters("qwertyuiopasdfghjklzxcvbnm");
    const uint codes[]{16,17,18,19,20,21,22,23,24,25,
                        30,31,32,33,34,35,36,37,38,
                        44,45,46,47,48,49,50};
    for (const char character : text)
    {
        const auto letter = QChar::fromLatin1(character);
        bool upper = character >= 'A' && character <= 'Z';
        const auto index = letters.indexOf(letter.toLower().toLatin1());
        uint code;
        Qt::Key key;
        if (index >= 0) {
            code = codes[index];
            key = Qt::Key(letter.toUpper().unicode());
        } else if (const auto digit = QByteArray("1234567890").indexOf(character); digit >= 0) {
            code = 2 + digit;
            key = Qt::Key(character);
        } else {
            switch (character) {
            case ' ': code=57; key=Qt::Key_Space; break;
            case '/': code=53; key=Qt::Key_Slash; break;
            case '.': code=52; key=Qt::Key_Period; break;
            case '-': code=12; key=Qt::Key_Minus; break;
            case '_': code=12; key=Qt::Key_Underscore; upper=true; break;
            default: return false;
            }
        }
        const auto modifiers = upper ? Qt::ShiftModifier : Qt::NoModifier;
        if (upper)
            nativeKeyEvent(window, QEvent::KeyPress, Qt::Key_Shift, modifiers, 42);
        nativeKeyEvent(window, QEvent::KeyPress, key, modifiers, code, QString(letter));
        nativeKeyEvent(window, QEvent::KeyRelease, key, modifiers, code, QString(letter));
        if (upper)
            nativeKeyEvent(window, QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier, 42);
    }
    return true;
}
