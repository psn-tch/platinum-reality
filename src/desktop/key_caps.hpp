#pragma once
#include <QObject>
#include <QVariantMap>
#include <memory>

struct xkb_context;
struct xkb_keymap;

// Key Caps' labels: what each key types for a keyboard layout, with Shift and Option (the
// layout's third level, AltGr) held or not, read from xkbcommon. The layout is the one
// applications are given ("us" unless the session chooses another).
class KeyCaps final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString layout READ layout WRITE setLayout NOTIFY layoutChanged)
  public:
    explicit KeyCaps(QObject *parent = nullptr);
    ~KeyCaps() override;
    QString layout() const { return m_layout; }
    void setLayout(const QString &layout);
    // Linux key code (evdev) -> the text it types, for the printable keys of a full keyboard.
    Q_INVOKABLE QVariantMap labels(bool shift, bool option) const;
    // The keys that are dead keys (an accent that waits for the next letter) with these modifiers:
    // code -> the accent shown on the key. 9.2.2's Key Caps outlines them.
    Q_INVOKABLE QVariantMap deadKeys(bool shift, bool option) const;

  signals:
    void layoutChanged();

  private:
    struct xkb_state *stateFor(bool shift, bool option) const;
    QString m_layout = "us";
    xkb_context *m_context = nullptr;
    xkb_keymap *m_keymap = nullptr;
};
