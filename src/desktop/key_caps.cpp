#include "key_caps.hpp"
#include <QHash>
#include <QRegularExpression>
#include <xkbcommon/xkbcommon.h>

KeyCaps::KeyCaps(QObject *parent) : QObject(parent)
{
    m_context = xkb_context_new(XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
    setLayout("us");
}
KeyCaps::~KeyCaps()
{
    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    if (m_context)
        xkb_context_unref(m_context);
}
void KeyCaps::setLayout(const QString &layout)
{
    // Layout names are short words ("us", "de", "us(dvorak)" is given as layout and variant).
    static const QRegularExpression valid("^[a-z]{2,8}(\\([a-z0-9_-]{1,24}\\))?$");
    if (!m_context || !valid.match(layout).hasMatch() || (layout == m_layout && m_keymap))
        return;
    QString name = layout, variant;
    if (const auto open = layout.indexOf('('); open > 0)
    {
        name = layout.left(open);
        variant = layout.mid(open + 1, layout.size() - open - 2);
    }
    const auto nameBytes = name.toLatin1(), variantBytes = variant.toLatin1();
    const xkb_rule_names names{"evdev", "pc105", nameBytes.constData(),
                               variant.isEmpty() ? nullptr : variantBytes.constData(), nullptr};
    auto *keymap = xkb_keymap_new_from_names(m_context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap)
        return;
    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    m_keymap = keymap;
    m_layout = layout;
    emit layoutChanged();
}
namespace
{
// The typing keys: digits row, three letter rows, and the keys beside them.
constexpr int typingKeys[]{41, 2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 16, 17, 18, 19, 20,
                           21, 22, 23, 24, 25, 26, 27, 43, 30, 31, 32, 33, 34, 35, 36, 37, 38,
                           39, 40, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 86};
} // namespace
xkb_state *KeyCaps::stateFor(bool shift, bool option) const
{
    auto *state = m_keymap ? xkb_state_new(m_keymap) : nullptr;
    if (!state)
        return nullptr;
    xkb_mod_mask_t mods = 0;
    if (const auto index = xkb_keymap_mod_get_index(m_keymap, XKB_MOD_NAME_SHIFT); shift && index != XKB_MOD_INVALID)
        mods |= 1u << index;
    // Option types a key's third level, as AltGr does on a PC keyboard.
    if (const auto index = xkb_keymap_mod_get_index(m_keymap, "Mod5"); option && index != XKB_MOD_INVALID)
        mods |= 1u << index;
    xkb_state_update_mask(state, mods, 0, 0, 0, 0, 0);
    return state;
}
QVariantMap KeyCaps::deadKeys(bool shift, bool option) const
{
    QVariantMap result;
    auto *state = stateFor(shift, option);
    if (!state)
        return result;
    static const QHash<xkb_keysym_t, QString> accents{
        {XKB_KEY_dead_grave, "`"},     {XKB_KEY_dead_acute, "´"},  {XKB_KEY_dead_circumflex, "ˆ"},
        {XKB_KEY_dead_tilde, "˜"},     {XKB_KEY_dead_diaeresis, "¨"}, {XKB_KEY_dead_cedilla, "¸"},
        {XKB_KEY_dead_abovering, "˚"}, {XKB_KEY_dead_caron, "ˇ"},  {XKB_KEY_dead_macron, "¯"}};
    for (const auto code : typingKeys)
    {
        const auto sym = xkb_state_key_get_one_sym(state, code + 8);
        if (sym >= XKB_KEY_dead_grave && sym <= XKB_KEY_dead_greek)
            result.insert(QString::number(code), accents.value(sym, "◌"));
    }
    xkb_state_unref(state);
    return result;
}
QVariantMap KeyCaps::labels(bool shift, bool option) const
{
    QVariantMap result;
    auto *state = stateFor(shift, option);
    if (!state)
        return result;
    for (const auto code : typingKeys)
    {
        char text[16]{};
        const auto size = xkb_state_key_get_utf8(state, code + 8, text, sizeof text);
        const auto label = size > 0 ? QString::fromUtf8(text, std::min<int>(size, sizeof text - 1)) : QString();
        // Control characters and combining-only results are shown blank.
        if (!label.isEmpty() && label.at(0).isPrint())
            result.insert(QString::number(code), label);
    }
    xkb_state_unref(state);
    return result;
}
